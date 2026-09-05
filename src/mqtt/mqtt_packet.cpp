#include "mqtt_packet.hpp"
#include <stdexcept>

namespace apr {

static void encode_remaining_length(size_t length, std::vector<uint8_t>& buf) {
    do {
        uint8_t d = length % 128;
        length /= 128;
        if (length > 0) {
            d |= 0x80;
        }
        buf.push_back(d);
    } while (length > 0);
}

static size_t decode_remaining_length(const uint8_t* data, size_t max_len, size_t& bytes_read) {
    size_t multiplier = 1;
    size_t value = 0;
    bytes_read = 0;
    uint8_t encoded_byte;
    do {
        if (bytes_read >= max_len) {
            throw std::runtime_error("Malformed remaining length");
        }
        encoded_byte = data[bytes_read++];
        value += (encoded_byte & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128 * 128 * 128) {
            throw std::runtime_error("Malformed remaining length field");
        }
    } while ((encoded_byte & 128) != 0);
    return value;
}

static std::string read_string(const uint8_t* data, size_t& offset) {
    uint16_t len = (data[offset] << 8) | data[offset + 1];
    offset += 2;
    std::string str(reinterpret_cast<const char*>(data + offset), len);
    offset += len;
    return str;
}

static void write_string(const std::string& str, std::vector<uint8_t>& buf) {
    uint16_t len = static_cast<uint16_t>(str.size());
    buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(len & 0xFF));
    buf.insert(buf.end(), str.begin(), str.end());
}

std::vector<uint8_t> mqtt_codec::encode_connack(uint8_t return_code) {
    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(mqtt_type::connack));
    packet.push_back(0x02); // remaining length
    packet.push_back(0x00); // session present flag
    packet.push_back(return_code);
    return packet;
}

std::vector<uint8_t> mqtt_codec::encode_puback(uint16_t packet_id) {
    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(mqtt_type::puback));
    packet.push_back(0x02);
    packet.push_back(static_cast<uint8_t>((packet_id >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>(packet_id & 0xFF));
    return packet;
}

std::vector<uint8_t> mqtt_codec::encode_suback(uint16_t packet_id, const std::vector<uint8_t>& return_codes) {
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>((packet_id >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(packet_id & 0xFF));
    payload.insert(payload.end(), return_codes.begin(), return_codes.end());

    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(mqtt_type::suback));
    encode_remaining_length(payload.size(), packet);
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

std::vector<uint8_t> mqtt_codec::encode_pingresp() {
    return {static_cast<uint8_t>(mqtt_type::pingresp), 0x00};
}

std::vector<uint8_t> mqtt_codec::encode_publish(const std::string& topic, const std::string& payload, uint8_t qos, uint16_t packet_id) {
    std::vector<uint8_t> body;
    write_string(topic, body);
    if (qos > 0) {
        body.push_back(static_cast<uint8_t>((packet_id >> 8) & 0xFF));
        body.push_back(static_cast<uint8_t>(packet_id & 0xFF));
    }
    body.insert(body.end(), payload.begin(), payload.end());

    uint8_t header = static_cast<uint8_t>(mqtt_type::publish) | ((qos & 0x03) << 1);
    std::vector<uint8_t> packet;
    packet.push_back(header);
    encode_remaining_length(body.size(), packet);
    packet.insert(packet.end(), body.begin(), body.end());
    return packet;
}

bool mqtt_codec::decode_connect(const uint8_t* data, size_t len, mqtt_connect& out_conn) {
    try {
        if (len < 2) return false;
        size_t bytes_read = 0;
        size_t rem_len = decode_remaining_length(data + 1, len - 1, bytes_read);
        size_t offset = 1 + bytes_read;

        if (len < offset + rem_len) return false;

        std::string proto_name = read_string(data, offset);
        if (proto_name != "MQTT" && proto_name != "MQIsdp") return false;

        uint8_t proto_level = data[offset++];
        uint8_t conn_flags = data[offset++];
        out_conn.keep_alive = (data[offset] << 8) | data[offset + 1];
        offset += 2;

        out_conn.client_id = read_string(data, offset);

        if (conn_flags & 0x04) { // Will flag
            read_string(data, offset); // Will Topic
            read_string(data, offset); // Will Message
        }

        if (conn_flags & 0x80) { // Username flag
            out_conn.username = read_string(data, offset);
        }
        if (conn_flags & 0x40) { // Password flag
            out_conn.password = read_string(data, offset);
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool mqtt_codec::decode_publish(const uint8_t* data, size_t len, mqtt_publish& out_pub) {
    try {
        if (len < 2) return false;
        uint8_t header = data[0];
        out_pub.qos = (header >> 1) & 0x03;
        out_pub.retain = (header & 0x01) != 0;

        size_t bytes_read = 0;
        size_t rem_len = decode_remaining_length(data + 1, len - 1, bytes_read);
        size_t offset = 1 + bytes_read;

        if (len < offset + rem_len) return false;

        out_pub.topic = read_string(data, offset);

        if (out_pub.qos > 0) {
            out_pub.packet_id = (data[offset] << 8) | data[offset + 1];
            offset += 2;
        }

        size_t payload_len = (1 + bytes_read + rem_len) - offset;
        out_pub.payload = std::string(reinterpret_cast<const char*>(data + offset), payload_len);
        return true;
    } catch (...) {
        return false;
    }
}

bool mqtt_codec::decode_subscribe(const uint8_t* data, size_t len, mqtt_subscribe& out_sub) {
    try {
        if (len < 2) return false;
        size_t bytes_read = 0;
        size_t rem_len = decode_remaining_length(data + 1, len - 1, bytes_read);
        size_t offset = 1 + bytes_read;

        if (len < offset + rem_len) return false;

        out_sub.packet_id = (data[offset] << 8) | data[offset + 1];
        offset += 2;

        size_t end_offset = 1 + bytes_read + rem_len;
        while (offset < end_offset) {
            std::string topic = read_string(data, offset);
            uint8_t requested_qos = data[offset++];
            out_sub.topics.push_back(topic);
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace apr
