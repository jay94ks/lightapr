cmake_minimum_required(VERSION 3.16)
project(lightapr VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch dependencies: nlohmann_json and asio
include(FetchContent)

# nlohmann_json
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

# Standalone Asio
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${asio_SOURCE_DIR}/asio/include
)

# Source files
set(CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/memory_tracker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/cli_options.cpp
)

set(MQTT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mqtt/mqtt_packet.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mqtt/websocket_codec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mqtt/mqtt_server.cpp
)

set(HTTP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http/http_server.cpp
)

# Platform selection
if(WIN32)
    set(PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/win32/win32_platform.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/linux/posix_platform.cpp)
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|Darwin")
    set(PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/bsd/bsd_platform.cpp)
else()
    set(PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/linux/posix_platform.cpp)
endif()

add_executable(lightapr
    src/main.cpp
    ${CORE_SOURCES}
    ${MQTT_SOURCES}
    ${HTTP_SOURCES}
    ${PLATFORM_SOURCES}
)

target_link_libraries(lightapr PRIVATE nlohmann_json::nlohmann_json)

if(WIN32)
    target_link_libraries(lightapr PRIVATE ws2_32 wsock32)
    target_compile_definitions(lightapr PRIVATE _WIN32_WINNT=0x0601)
else()
    target_link_libraries(lightapr PRIVATE pthread)
endif()

target_compile_definitions(lightapr PRIVATE ASIO_STANDALONE)

# Tests
enable_testing()
add_subdirectory(tests)

# SDK & Examples
add_subdirectory(sdk/cpp)
add_subdirectory(examples/cpp)
