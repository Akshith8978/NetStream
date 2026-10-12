cmake_minimum_required(VERSION 3.16)
project(NetStream VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wcast-align
    -Wformat=2
    -Wnull-dereference
)

# Sanitizer options
option(ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(BUILD_TESTS "Build test suite" ON)

if(ENABLE_ASAN_UBSAN AND ENABLE_TSAN)
    message(FATAL_ERROR "Cannot enable AddressSanitizer and ThreadSanitizer simultaneously.")
endif()

if(ENABLE_ASAN_UBSAN)
    message(STATUS "Enabling AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan)")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

if(ENABLE_TSAN)
    message(STATUS "Enabling ThreadSanitizer (TSan)")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)

# Core library
set(CORE_SOURCES
    src/common/fd.cpp
    src/common/logger.cpp
    src/common/protocol.cpp
    src/common/sha256.cpp
    src/tcp/connection.cpp
    src/tcp/thread_pool.cpp
    src/tcp/tcp_server.cpp
    src/udp/packet.cpp
    src/udp/jitter_buffer.cpp
    src/udp/streamer.cpp
    src/benchmark/metrics.cpp
)

add_library(netstream_core STATIC ${CORE_SOURCES})
target_link_libraries(netstream_core PUBLIC Threads::Threads)

# Executables
add_executable(netstream_server src/main.cpp)
target_link_libraries(netstream_server PRIVATE netstream_core)

add_executable(netstream_client apps/tcp_client.cpp)
target_link_libraries(netstream_client PRIVATE netstream_core)

add_executable(netstream_udp_sender apps/udp_sender.cpp)
target_link_libraries(netstream_udp_sender PRIVATE netstream_core)

add_executable(netstream_udp_receiver apps/udp_receiver.cpp)
target_link_libraries(netstream_udp_receiver PRIVATE netstream_core)

add_executable(netstream_load_generator apps/load_generator.cpp)
target_link_libraries(netstream_load_generator PRIVATE netstream_core)

# Tests
if(BUILD_TESTS)
    enable_testing()

    add_executable(test_protocol tests/unit/test_protocol.cpp)
    target_link_libraries(test_protocol PRIVATE netstream_core)
    add_test(NAME test_protocol COMMAND test_protocol)

    add_executable(test_thread_pool tests/unit/test_thread_pool.cpp)
    target_link_libraries(test_thread_pool PRIVATE netstream_core)
    add_test(NAME test_thread_pool COMMAND test_thread_pool)

    add_executable(test_udp_packet tests/unit/test_udp_packet.cpp)
    target_link_libraries(test_udp_packet PRIVATE netstream_core)
    add_test(NAME test_udp_packet COMMAND test_udp_packet)

    add_executable(test_jitter_buffer tests/unit/test_jitter_buffer.cpp)
    target_link_libraries(test_jitter_buffer PRIVATE netstream_core)
    add_test(NAME test_jitter_buffer COMMAND test_jitter_buffer)

    add_executable(test_tcp_server tests/integration/test_tcp_server.cpp)
    target_link_libraries(test_tcp_server PRIVATE netstream_core)
    add_test(NAME test_tcp_server COMMAND test_tcp_server)

    add_executable(test_udp_streaming tests/integration/test_udp_streaming.cpp)
    target_link_libraries(test_udp_streaming PRIVATE netstream_core)
    add_test(NAME test_udp_streaming COMMAND test_udp_streaming)
endif()
