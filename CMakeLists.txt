cmake_minimum_required(VERSION 3.20)
project(http_server LANGUAGES CXX)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_SANITIZERS "Build with ASan + UBSan" OFF)
if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

option(ENABLE_TSAN "Build with ThreadSanitizer" OFF)
if(ENABLE_TSAN)
  if(ENABLE_SANITIZERS)
    message(FATAL_ERROR "ENABLE_TSAN cannot be combined with ENABLE_SANITIZERS")
  endif()
  add_compile_options(-fsanitize=thread)
  add_link_options(-fsanitize=thread)
endif()

find_package(Threads REQUIRED)

# Core library: all real logic lives here so tests can use it
add_library(core src/util.cpp src/socket.cpp src/thread_pool.cpp src/request.cpp)
target_include_directories(core PUBLIC include)
target_compile_options(core PRIVATE -Wall -Wextra -Wpedantic)

# The server executable
add_executable(server src/main.cpp)
target_link_libraries(server PRIVATE core Threads::Threads)

# Unit tests (GoogleTest is downloaded automatically)
include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(core_tests tests/util_test.cpp tests/socket_test.cpp tests/thread_pool_test.cpp tests/request_test.cpp)
target_link_libraries(core_tests PRIVATE core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(core_tests)
