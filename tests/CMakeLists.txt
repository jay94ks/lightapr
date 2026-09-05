add_executable(unit_tests
    unit/test_registry.cpp
    unit/test_mqtt_packet.cpp
    unit/test_websocket.cpp
    ${CORE_SOURCES}
    ${MQTT_SOURCES}
)

target_link_libraries(unit_tests PRIVATE nlohmann_json::nlohmann_json)

if(WIN32)
    target_link_libraries(unit_tests PRIVATE ws2_32 wsock32)
    target_compile_definitions(unit_tests PRIVATE _WIN32_WINNT=0x0601)
else()
    target_link_libraries(unit_tests PRIVATE pthread)
endif()

target_compile_definitions(unit_tests PRIVATE ASIO_STANDALONE)

add_test(NAME unit_tests COMMAND unit_tests)
