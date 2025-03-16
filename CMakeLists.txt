
cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


ExternalProject_Add(
    Boost
    URL https://boostorg.jfrog.io/artifactory/main/release/1.82.0/source/boost_1_82_0.tar.gz
    CONFIGURE_COMMAND ./bootstrap.sh
    BUILD_COMMAND ./b2 --with-system --with-filesystem --with-thread  --with-atomic --with-coroutine --with-context
    INSTALL_COMMAND ""  # Skip installation
    BUILD_IN_SOURCE TRUE
)

project(sokka)

add_executable(main src/main.cpp)

# set(LIBEVENT_ROOT "./libevent-2.1.12-stable")
set(BOOST_ROOT "./boost_1_82_0/build")

# target_include_directories(main PRIVATE ${LIBEVENT_ROOT}/include)
# target_link_directories(main PRIVATE ${LIBEVENT_ROOT}/lib)
# 
target_include_directories(main PRIVATE ${BOOST_ROOT}/include)
target_link_directories(main PRIVATE ${BOOST_ROOT}/lib)
target_link_libraries(main PRIVATE boost_system boost_filesystem boost_thread  boost_atomic boost_coroutine boost_context)
target_link_libraries(main PRIVATE pthread crypto ssl)

target_compile_options(main PRIVATE -fcoroutines)
