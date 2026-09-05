add_executable(unit_tests
    unit/test_registry.cpp
    unit/test_mqtt_packet.cpp
    unit/test_websocket.cpp
    unit/test_cli_options.cpp
    unit/test_stream_accumulator.cpp
    unit/test_registry_concurrency.cpp
    unit/test_http_server.cpp
    unit/test_connection_guard.cpp
    unit/test_memory_tracker.cpp
    ${CORE_SOURCES}
    ${MQTT_SOURCES}
    ${HTTP_SOURCES}
)

target_link_libraries(unit_tests PRIVATE nlohmann_json::nlohmann_json apr_embedded_assets)

if(WIN32)
    target_link_libraries(unit_tests PRIVATE ws2_32 wsock32)
    target_compile_definitions(unit_tests PRIVATE _WIN32_WINNT=0x0601)
else()
    target_link_libraries(unit_tests PRIVATE pthread)
endif()

target_compile_definitions(unit_tests PRIVATE ASIO_STANDALONE)

add_test(NAME unit_tests COMMAND unit_tests)
