#include "boost/asio.hpp"
#include "boost/asio/awaitable.hpp"
#include "boost/asio/posix/stream_descriptor.hpp"
#include "boost/beast.hpp"
#include "boost/beast/http/chunk_encode.hpp"
#include "boost/beast/http/status.hpp"
#include "boost/beast/http/string_body.hpp"
#include "boost/system/detail/error_code.hpp"
#include "boost/system/system_error.hpp"
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <stdio.h>

namespace http = boost::beast::http;
namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;

asio::awaitable<void> handle_request_async(tcp::socket socket,
                                           asio::io_context &ioc) {

  beast::flat_buffer buffer;
  http::request<http::string_body> req;

  co_await http::async_read(socket, buffer, req, asio::use_awaitable);
  http::response<http::string_body> res;
  std::string file_path;

  if (req.target().starts_with("/")) {
    file_path += "." + std::string(req.target());
  } else {
    file_path += "./" + std::string(req.target());
  }

  asio::posix::stream_descriptor file(ioc);

  int fd = open(file_path.c_str(), O_RDONLY | O_NONBLOCK);

  if (fd == -1) {
    res.result(http::status::not_found);
    res.set(http::field::content_type, "text/plain");
    res.body() = "404 Not Found";
    printf("file %s does not exist\n", file_path.c_str());
  } else {
    printf("Reading file %s\n", file_path.c_str());
    res.result(http::status::ok);
    res.set(http::field::content_type, "text/plain");

    std::string body;

    file.assign(fd);

    char buffer[4096];

    while (true) {

      try {
        auto bytes_read = co_await file.async_read_some(asio::buffer(buffer),
                                                        asio::use_awaitable);

        if (bytes_read == 0) {
          break;
        }

        body.append(buffer, bytes_read);


      } catch (const boost::system::system_error &e) {
        if (e.code() == boost::asio::error::eof) {
          break;
        } else {

          printf("Request handling error %s\n", e.what());
        }
      }
    }
    
    res.body() = body;
  }

  res.prepare_payload();
  co_await http::async_write(socket, res, asio::use_awaitable);
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
