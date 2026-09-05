#include "../../src/mqtt/mqtt_packet.hpp"
#include "apr/mqtt_topic.hpp"
#include <cassert>
#include <iostream>
#include <vector>

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

// Builds a minimal, well-formed CONNECT packet: protocol name "MQTT", clean
// session flag only (no username/password/will), keep_alive=60, client_id
// as given.
static std::vector<uint8_t> build_connect_packet(const std::string& client_id) {
    std::vector<uint8_t> body;
    body.push_back(0x00); body.push_back(0x04);
    body.insert(body.end(), {'M', 'Q', 'T', 'T'});
    body.push_back(0x04); // protocol level
    body.push_back(0x02); // connect flags: clean session
    body.push_back(0x00); body.push_back(0x3C); // keep alive = 60

    body.push_back(static_cast<uint8_t>((client_id.size() >> 8) & 0xFF));
    body.push_back(static_cast<uint8_t>(client_id.size() & 0xFF));
    body.insert(body.end(), client_id.begin(), client_id.end());

    std::vector<uint8_t> packet;
    packet.push_back(0x10); // CONNECT
    packet.push_back(static_cast<uint8_t>(body.size())); // remaining length (<128)
    packet.insert(packet.end(), body.begin(), body.end());
    return packet;
}

void test_mqtt_codec_decode_connect_valid() {
    auto packet = build_connect_packet("test");
    apr::mqtt_connect conn;
    bool ok = apr::mqtt_codec::decode_connect(packet.data(), packet.size(), conn);
    assert(ok);
    assert(conn.client_id == "test");
    assert(conn.keep_alive == 60);
    std::cout << "[PASS] test_mqtt_codec_decode_connect_valid" << std::endl;
}

void test_mqtt_codec_decode_connect_truncated_client_id_rejected() {
    auto packet = build_connect_packet("test");
    // Overwrite the client_id length prefix (last 6 bytes: 2-byte length + "test")
    // to falsely claim a huge length far beyond the actual buffer - this is
    // exactly the out-of-bounds read read_string() must now reject instead of
    // reading past the end of `packet`.
    size_t len_prefix_pos = packet.size() - 6;
    packet[len_prefix_pos] = 0xFF;
    packet[len_prefix_pos + 1] = 0xFF;

    apr::mqtt_connect conn;
    bool ok = apr::mqtt_codec::decode_connect(packet.data(), packet.size(), conn);
    assert(!ok); // must fail cleanly, not read out of bounds

    std::cout << "[PASS] test_mqtt_codec_decode_connect_truncated_client_id_rejected" << std::endl;
}

void test_mqtt_codec_decode_connect_truncated_buffer_rejected() {
    auto packet = build_connect_packet("test");
    // Pass a buffer shorter than the packet actually claims to be.
    apr::mqtt_connect conn;
    bool ok = apr::mqtt_codec::decode_connect(packet.data(), packet.size() - 3, conn);
    assert(!ok);
    std::cout << "[PASS] test_mqtt_codec_decode_connect_truncated_buffer_rejected" << std::endl;
}

void test_mqtt_topic_wildcards() {
    using apr::mqtt_topic::matches;

    assert(matches("apr/worker", "apr/worker"));
    assert(!matches("apr/worker", "apr/other"));

    assert(matches("#", "anything/at/all"));
    assert(matches("apr/#", "apr/worker"));
    assert(matches("apr/#", "apr/worker/sub"));
    assert(!matches("apr/#", "other/worker"));

    assert(matches("apr/+/status", "apr/worker/status"));
    assert(matches("apr/+/status", "apr/other/status"));
    assert(!matches("apr/+/status", "apr/worker/other"));
    assert(!matches("apr/+", "apr/worker/extra"));

    std::cout << "[PASS] test_mqtt_topic_wildcards" << std::endl;
}

void run_mqtt_packet_tests() {
    test_mqtt_codec_connack();
    test_mqtt_codec_publish();
    test_mqtt_codec_decode_connect_valid();
    test_mqtt_codec_decode_connect_truncated_client_id_rejected();
    test_mqtt_codec_decode_connect_truncated_buffer_rejected();
    test_mqtt_topic_wildcards();
}
