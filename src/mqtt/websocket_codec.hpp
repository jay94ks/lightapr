#ifndef APR_WEBSOCKET_CODEC_HPP
#define APR_WEBSOCKET_CODEC_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace apr {

struct ws_frame {
    uint8_t opcode{0}; // 0x1: text, 0x2: binary, 0x8: close, 0x9: ping, 0xA: pong
    bool fin{true};
    std::string payload;
};

class websocket_codec {
public:
    // SHA-1 digest calculation (20 bytes)
    static std::vector<uint8_t> sha1(const std::string& input);

    // Base64 encoding
    static std::string base64_encode(const std::vector<uint8_t>& data);

    // Compute Sec-WebSocket-Accept header value from Sec-WebSocket-Key
    static std::string compute_accept_key(const std::string& client_key);

    // Parse HTTP WebSocket Upgrade Handshake Request
    static bool parse_handshake_request(const std::string& request, std::string& out_key, std::string& out_protocol);

    // Generate HTTP WebSocket Upgrade Handshake Response (101 Switching Protocols)
    static std::string generate_handshake_response(const std::string& accept_key, const std::string& protocol = "mqtt");

    // Decode WebSocket frame from buffer. Returns bytes consumed, or 0 if incomplete.
    static size_t decode_frame(const uint8_t* data, size_t len, ws_frame& out_frame);

    // Encode payload into WebSocket frame (Binary or Text)
    static std::vector<uint8_t> encode_frame(const uint8_t* payload, size_t len, uint8_t opcode = 0x02);
};

} // namespace apr

#endif // APR_WEBSOCKET_CODEC_HPP
