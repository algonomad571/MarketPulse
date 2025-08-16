cmake_minimum_required(VERSION 3.20)
project(md_system_cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(WITH_BINANCE "Build with Binance WebSocket connector" OFF)
option(WITH_CLICKHOUSE "Build with ClickHouse analytics" OFF)
option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3 -march=native")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -fsanitize=address -fsanitize=thread")
endif()

# Find packages
find_package(Boost REQUIRED COMPONENTS system filesystem thread)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Third-party libraries (simplified for demo - would use vcpkg/conan in production)
add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE external/spdlog/include)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE external/nlohmann_json/include)

add_library(moodycamel INTERFACE)
target_include_directories(moodycamel INTERFACE external/concurrentqueue)

# Core library
add_library(md_core
    src/common/frame.cpp
    src/common/symbol_registry.cpp
    src/common/config.cpp
    src/common/metrics.cpp
    src/common/crc32.cpp
    src/feed/mock_feed.cpp
    src/normalize/normalizer.cpp
    src/publisher/pub_server.cpp
    src/recorder/recorder.cpp
    src/replay/replayer.cpp
    src/ctrl/control_server.cpp
)

target_include_directories(md_core PUBLIC src)
target_link_libraries(md_core 
    PUBLIC 
        Boost::system 
        Boost::filesystem 
        Boost::thread
        Threads::Threads
        spdlog
        nlohmann_json
        moodycamel
)

# Main executable
add_executable(md_core_main src/main_core.cpp)
target_link_libraries(md_core_main md_core)

# Optional Binance connector
if(WITH_BINANCE)
    target_sources(md_core PRIVATE src/feed/binance_ws.cpp)
    target_compile_definitions(md_core PRIVATE WITH_BINANCE=1)
endif()

# Optional ClickHouse
if(WITH_CLICKHOUSE)
    target_sources(md_core PRIVATE src/analytics/clickhouse_writer.cpp)
    target_compile_definitions(md_core PRIVATE WITH_CLICKHOUSE=1)
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Benchmarks
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()