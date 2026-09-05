cmake_minimum_required(VERSION 3.16)
project(apr_examples_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cpp_http_node http_node/main.cpp)
target_link_libraries(cpp_http_node PRIVATE apr_sdk_cpp nlohmann_json::nlohmann_json)

add_executable(cpp_worker_node worker_node/main.cpp)
target_link_libraries(cpp_worker_node PRIVATE apr_sdk_cpp nlohmann_json::nlohmann_json)
