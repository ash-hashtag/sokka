#include "boost/asio.hpp"
#include "boost/asio/awaitable.hpp"
#include "boost/asio/deadline_timer.hpp"
#include "boost/asio/posix/stream_descriptor.hpp"
#include "boost/beast.hpp"
#include "boost/beast/http/buffer_body.hpp"
#include "boost/beast/http/chunk_encode.hpp"
#include "boost/beast/http/empty_body.hpp"
#include "boost/beast/http/serializer.hpp"
#include "boost/beast/http/status.hpp"
#include "boost/beast/http/string_body.hpp"
#include "boost/beast/http/write.hpp"
#include "boost/system/detail/error_code.hpp"
#include "boost/system/system_error.hpp"
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <stdio.h>
#include <string>
#include <sys/stat.h>

namespace http = boost::beast::http;
namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;

asio::awaitable<void> handle_request_async(tcp::socket socket,
                                           asio::io_context &ioc) {

  beast::flat_buffer buffer;
  http::request<http::string_body> req;

  co_await http::async_read(socket, buffer, req, asio::use_awaitable);
  std::string file_path;

  if (req.target().starts_with("/")) {
    file_path += "." + std::string(req.target());
  } else {
    file_path += "./" + std::string(req.target());
  }

  asio::posix::stream_descriptor file(ioc);

  int fd = open(file_path.c_str(), O_RDONLY | O_NONBLOCK);

  if (fd == -1) {
    http::response<http::string_body> res;
    res.result(http::status::not_found);
    res.set(http::field::content_type, "text/plain");
    res.body() = "404 Not Found";
    res.prepare_payload();
    co_await http::async_write(socket, res, asio::use_awaitable);
    printf("file %s does not exist\n", file_path.c_str());
  } else {

    struct stat info;
    if (fstat(fd, &info) == -1) {
      http::response<http::string_body> res;
      res.result(http::status::not_found);
      res.set(http::field::content_type, "text/plain");
      res.body() = "404 Not Found";
      res.prepare_payload();
      co_await http::async_write(socket, res, asio::use_awaitable);
      printf("reading file %s has some problem errno %d \n", file_path.c_str(),
             errno);
      co_return;
    }

    if (!S_ISREG(info.st_mode)) {
      http::response<http::string_body> res;
      res.result(http::status::not_found);
      res.set(http::field::content_type, "text/plain");
      res.body() = "404 Not Found";
      res.prepare_payload();
      co_await http::async_write(socket, res, asio::use_awaitable);
      printf(" %s aint a file ", file_path.c_str());
      co_return;
    }

    printf("Reading file %s with length %ld\n", file_path.c_str(),
           info.st_size);
    http::response<http::buffer_body> res;

    res.result(http::status::ok);
    res.version(11);
    res.set(http::field::content_type, "text/plain");
    // res.set(http::field::transfer_encoding, "chunked");
    res.set(http::field::content_length, std::to_string(info.st_size));

    res.body().data = nullptr;
    res.body().more = true;

    http::response_serializer<http::buffer_body, http::fields> sr(res);

    co_await http::async_write_header(socket, sr, asio::use_awaitable);

    file.assign(fd);

#define BUFFER_CAPACITY 4096

    char buffer[BUFFER_CAPACITY];

    while (true) {
      try {

        // simulating throttle
        asio::deadline_timer timer(ioc, boost::posix_time::seconds(1));
        co_await timer.async_wait(asio::use_awaitable);

        auto bytes_read = co_await file.async_read_some(
            asio::buffer(buffer, sizeof(buffer)), asio::use_awaitable);

        if (bytes_read == 0) {
          break;
        }

        res.body().data = buffer;
        res.body().size = bytes_read;
        res.body().more = true;

        co_await http::async_write(socket, sr, asio::use_awaitable);

      } catch (const boost::system::system_error &e) {
        if (e.code() == http::error::need_buffer) {
          continue;
        }
        if (e.code() == boost::asio::error::eof) {
          break;
        } else {
          printf("request hander error %s\n", e.what());
          break;
        }
      }
    }

    res.body().data = nullptr;
    res.body().more = false;
    co_await http::async_write(socket, sr, asio::use_awaitable);
  }
}

asio::awaitable<void> accept_connections(tcp::acceptor acceptor,
                                         asio::io_context &ioc) {
  while (true) {
    auto socket = co_await acceptor.async_accept(asio::use_awaitable);
    asio::co_spawn(socket.get_executor(),
                   handle_request_async(std::move(socket), ioc),
                   asio::detached);
  }
}

void start_server(uint16_t port) {
  boost::asio::io_context ioc;
  tcp::acceptor acceptor(ioc, {tcp::v4(), port});

  asio::co_spawn(ioc, accept_connections(std::move(acceptor), ioc),
                 asio::detached);

  printf("Server started at ::%d\n", port);

  ioc.run();
}

int main() {

  start_server(8080);

  return 0;
}
