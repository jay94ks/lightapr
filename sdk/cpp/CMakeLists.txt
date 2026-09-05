cmake_minimum_required(VERSION 3.16)
project(apr_sdk_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(apr_sdk_cpp STATIC
    src/apr_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/mqtt/mqtt_packet.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/mqtt/websocket_codec.cpp
)

target_include_directories(apr_sdk_cpp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${asio_SOURCE_DIR}/asio/include
)

target_link_libraries(apr_sdk_cpp PRIVATE nlohmann_json::nlohmann_json)

if(WIN32)
    target_link_libraries(apr_sdk_cpp PRIVATE ws2_32 wsock32)
    target_compile_definitions(apr_sdk_cpp PRIVATE _WIN32_WINNT=0x0601)
else()
    target_link_libraries(apr_sdk_cpp PRIVATE pthread)
endif()

target_compile_definitions(apr_sdk_cpp PRIVATE ASIO_STANDALONE)
