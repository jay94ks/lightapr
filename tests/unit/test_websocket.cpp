#include "../../src/mqtt/websocket_codec.hpp"
#include <cassert>
#include <iostream>

void test_websocket_sha1_and_accept_key() {
    // RFC 6455 test key example
    std::string client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::string accept_key = apr::websocket_codec::compute_accept_key(client_key);
    assert(accept_key == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    std::cout << "[PASS] test_websocket_sha1_and_accept_key" << std::endl;
}

void test_websocket_handshake() {
    std::string req =
        "GET /mqtt HTTP/1.1\r\n"
        "Host: server.example.com\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Protocol: mqtt\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    std::string key, proto;
    bool ok = apr::websocket_codec::parse_handshake_request(req, key, proto);
    assert(ok);
    assert(key == "dGhlIHNhbXBsZSBub25jZQ==");
    assert(proto == "mqtt");

    std::string resp = apr::websocket_codec::generate_handshake_response("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "mqtt");
    assert(resp.find("101 Switching Protocols") != std::string::npos);
    assert(resp.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
    std::cout << "[PASS] test_websocket_handshake" << std::endl;
}

void test_websocket_frame_encode_decode() {
    std::string payload = "Hello MQTT WebSocket";
    auto encoded = apr::websocket_codec::encode_frame(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), 0x02); // Binary

    apr::ws_frame decoded;
    size_t consumed = apr::websocket_codec::decode_frame(encoded.data(), encoded.size(), decoded);
    assert(consumed == encoded.size());
    assert(decoded.opcode == 0x02);
    assert(decoded.payload == payload);

    std::cout << "[PASS] test_websocket_frame_encode_decode" << std::endl;
}

void run_websocket_tests() {
    test_websocket_sha1_and_accept_key();
    test_websocket_handshake();
    test_websocket_frame_encode_decode();
}
