#include "websocket_codec.hpp"
#include <sstream>
#include <cstring>
#include <algorithm>

namespace apr {

static inline uint32_t left_rotate(uint32_t value, size_t count) {
    return (value << count) | (value >> (32 - count));
}

std::vector<uint8_t> websocket_codec::sha1(const std::string& input) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    uint64_t bit_len = input.size() * 8;

    std::vector<uint8_t> msg(input.begin(), input.end());
    msg.push_back(0x80);

    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }

    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (size_t i = 16; i < 80; ++i) {
            w[i] = left_rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (size_t i = 0; i < 80; ++i) {
            uint32_t f = 0;
            uint32_t k = 0;

            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = left_rotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = left_rotate(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::vector<uint8_t> digest(20);
    uint32_t h[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        digest[i * 4]     = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
    }
    return digest;
}

std::string websocket_codec::base64_encode(const std::vector<uint8_t>& data) {
    static const char b64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string ret;
    int i = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];

    size_t in_len = data.size();
    auto bytes = data.data();

    while (in_len--) {
        char_array_3[i++] = *(bytes++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++) {
                ret += b64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (int j = 0; j < i + 1; j++) {
            ret += b64_chars[char_array_4[j]];
        }

        while (i++ < 3) {
            ret += '=';
        }
    }

    return ret;
}

std::string websocket_codec::compute_accept_key(const std::string& client_key) {
    static const std::string magic_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string concatenated = client_key + magic_guid;
    auto sha = sha1(concatenated);
    return base64_encode(sha);
}

bool websocket_codec::parse_handshake_request(const std::string& request, std::string& out_key, std::string& out_protocol) {
    if (request.find("GET ") != 0 && request.find("get ") != 0) {
        return false;
    }
    if (request.find("Upgrade: websocket") == std::string::npos &&
        request.find("upgrade: websocket") == std::string::npos &&
        request.find("Upgrade: WebSocket") == std::string::npos) {
        return false;
    }

    std::stringstream ss(request);
    std::string line;
    out_key.clear();
    out_protocol.clear();

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string header_name = line.substr(0, colon);
            std::string header_val = line.substr(colon + 1);

            // Strip leading spaces
            while (!header_val.empty() && header_val.front() == ' ') header_val.erase(0, 1);

            std::string lower_name = header_name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

            if (lower_name == "sec-websocket-key") {
                out_key = header_val;
            } else if (lower_name == "sec-websocket-protocol") {
                out_protocol = header_val;
            }
        }
    }

    return !out_key.empty();
}

std::string websocket_codec::generate_handshake_response(const std::string& accept_key, const std::string& protocol) {
    std::stringstream ss;
    ss << "HTTP/1.1 101 Switching Protocols\r\n";
    ss << "Upgrade: websocket\r\n";
    ss << "Connection: Upgrade\r\n";
    ss << "Sec-WebSocket-Accept: " << accept_key << "\r\n";
    if (!protocol.empty()) {
        ss << "Sec-WebSocket-Protocol: " << protocol << "\r\n";
    }
    ss << "\r\n";
    return ss.str();
}

size_t websocket_codec::decode_frame(const uint8_t* data, size_t len, ws_frame& out_frame) {
    if (len < 2) return 0; // Not enough header data

    uint8_t b0 = data[0];
    uint8_t b1 = data[1];

    out_frame.fin = (b0 & 0x80) != 0;
    out_frame.opcode = b0 & 0x0F;

    bool masked = (b1 & 0x80) != 0;
    uint64_t payload_len = b1 & 0x7F;

    size_t header_len = 2;

    if (payload_len == 126) {
        if (len < 4) return 0;
        payload_len = (static_cast<uint64_t>(data[2]) << 8) | data[3];
        header_len += 2;
    } else if (payload_len == 127) {
        if (len < 10) return 0;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | data[2 + i];
        }
        header_len += 8;
    }

    uint8_t mask[4] = {0};
    if (masked) {
        if (len < header_len + 4) return 0;
        std::memcpy(mask, data + header_len, 4);
        header_len += 4;
    }

    if (len < header_len + payload_len) return 0; // Incomplete frame

    out_frame.payload.resize(payload_len);
    const uint8_t* payload_src = data + header_len;

    if (masked) {
        for (size_t i = 0; i < payload_len; ++i) {
            out_frame.payload[i] = payload_src[i] ^ mask[i % 4];
        }
    } else {
        std::memcpy(&out_frame.payload[0], payload_src, payload_len);
    }

    return header_len + payload_len;
}

std::vector<uint8_t> websocket_codec::encode_frame(const uint8_t* payload, size_t len, uint8_t opcode) {
    std::vector<uint8_t> frame;
    frame.push_back(0x80 | (opcode & 0x0F)); // FIN bit set

    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    frame.insert(frame.end(), payload, payload + len);
    return frame;
}

} // namespace apr
