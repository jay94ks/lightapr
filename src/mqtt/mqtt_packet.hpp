#ifndef APR_MQTT_PACKET_HPP
#define APR_MQTT_PACKET_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace apr {

enum class mqtt_type : uint8_t {
    connect     = 0x10,
    connack     = 0x20,
    publish     = 0x30,
    puback      = 0x40,
    subscribe   = 0x82,
    suback      = 0x90,
    pingreq     = 0xC0,
    pingresp    = 0xD0,
    disconnect  = 0xE0
};

struct mqtt_connect {
    std::string client_id;
    std::string username;
    std::string password;
    uint16_t keep_alive{60};
};

struct mqtt_publish {
    std::string topic;
    std::string payload;
    uint16_t packet_id{0};
    uint8_t qos{0};
    bool retain{false};
};

struct mqtt_subscribe {
    uint16_t packet_id{0};
    std::vector<std::string> topics;
};

class mqtt_codec {
public:
    static std::vector<uint8_t> encode_connack(uint8_t return_code = 0);
    static std::vector<uint8_t> encode_puback(uint16_t packet_id);
    static std::vector<uint8_t> encode_suback(uint16_t packet_id, const std::vector<uint8_t>& return_codes);
    static std::vector<uint8_t> encode_pingresp();
    static std::vector<uint8_t> encode_publish(const std::string& topic, const std::string& payload, uint8_t qos = 0, uint16_t packet_id = 0);

    static bool decode_connect(const uint8_t* data, size_t len, mqtt_connect& out_conn);
    static bool decode_publish(const uint8_t* data, size_t len, mqtt_publish& out_pub);
    static bool decode_subscribe(const uint8_t* data, size_t len, mqtt_subscribe& out_sub);
};

} // namespace apr

#endif // APR_MQTT_PACKET_HPP
