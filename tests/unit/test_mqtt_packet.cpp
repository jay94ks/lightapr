#include "../../src/mqtt/mqtt_packet.hpp"
#include <cassert>
#include <iostream>

void test_mqtt_codec_connack() {
    auto bytes = apr::mqtt_codec::encode_connack(0x00);
    assert(bytes.size() == 4);
    assert(bytes[0] == 0x20); // CONNACK
    assert(bytes[1] == 0x02); // Remaining length
    assert(bytes[3] == 0x00); // Return code 0
    std::cout << "[PASS] test_mqtt_codec_connack" << std::endl;
}

void test_mqtt_codec_publish() {
    std::string topic = "apr/node/meta";
    std::string payload = "{\"role\":\"test\"}";
    auto bytes = apr::mqtt_codec::encode_publish(topic, payload);

    apr::mqtt_publish pub;
    bool success = apr::mqtt_codec::decode_publish(bytes.data(), bytes.size(), pub);
    assert(success);
    assert(pub.topic == topic);
    assert(pub.payload == payload);
    std::cout << "[PASS] test_mqtt_codec_publish" << std::endl;
}

void run_mqtt_packet_tests() {
    test_mqtt_codec_connack();
    test_mqtt_codec_publish();
}
