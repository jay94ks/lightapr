#include "mqtt_server.hpp"
#include "mqtt_packet.hpp"
#include "websocket_codec.hpp"
#include "apr/memory_tracker.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

namespace apr {

static bool topic_matches(const std::string& pattern, const std::string& topic) {
    if (pattern == "#") return true;
    if (pattern == topic) return true;

    size_t p_idx = 0, t_idx = 0;
    while (p_idx < pattern.size() && t_idx < topic.size()) {
        if (pattern[p_idx] == '#') {
            return true;
        }
        if (pattern[p_idx] == '+') {
            while (p_idx < pattern.size() && pattern[p_idx] != '/') p_idx++;
            while (t_idx < topic.size() && topic[t_idx] != '/') t_idx++;
            if (p_idx < pattern.size() && pattern[p_idx] == '/') p_idx++;
            if (t_idx < topic.size() && topic[t_idx] == '/') t_idx++;
            continue;
        }
        if (pattern[p_idx] != topic[t_idx]) {
            return false;
        }
        p_idx++;
        t_idx++;
    }

    if (p_idx == pattern.size() && t_idx == topic.size()) return true;
    if (p_idx < pattern.size() && pattern[p_idx] == '#' && p_idx == pattern.size() - 1) return true;
    return false;
}

mqtt_session::mqtt_session(asio::ip::tcp::socket socket, registry& reg, const std::string& access_key, close_callback on_close)
    : socket_(std::move(socket)), registry_(reg), access_key_(access_key), on_close_(std::move(on_close)) {
    std::error_code ec;
    auto remote = socket_.remote_endpoint(ec);
    if (!ec) {
        peer_ip_ = remote.address().to_string();
    }
}

mqtt_session::~mqtt_session() {
    close_session();
}

void mqtt_session::close_session() {
    if (!closed_) {
        closed_ = true;
        std::error_code ec;
        socket_.close(ec);
        if (!node_id_.empty()) {
            registry_.mark_node_grace(node_id_);
        }
    }
}

void mqtt_session::start() {
    do_read();
}

void mqtt_session::send_raw(const std::vector<uint8_t>& data) {
    auto self = shared_from_this();
    memory_tracker::instance().add_mqtt_bytes(data.size());

    std::shared_ptr<std::vector<uint8_t>> send_buf;
    if (is_websocket_) {
        auto framed = websocket_codec::encode_frame(data.data(), data.size(), 0x02);
        send_buf = std::make_shared<std::vector<uint8_t>>(std::move(framed));
    } else {
        send_buf = std::make_shared<std::vector<uint8_t>>(data);
    }

    asio::async_write(socket_, asio::buffer(*send_buf), [self, this, send_buf](std::error_code ec, size_t /*bytes*/) {
        if (ec) {
            LOG_WARN("MQTT write error: " + ec.message());
            close_session();
            if (on_close_) on_close_(self);
        }
    });
}

bool mqtt_session::matches_topic(const std::string& topic) const {
    std::lock_guard<std::mutex> lock(sub_mutex_);
    for (const auto& pat : subscriptions_) {
        if (topic_matches(pat, topic)) {
            return true;
        }
    }
    return false;
}

void mqtt_session::do_read() {
    auto self = shared_from_this();
    socket_.async_read_some(asio::buffer(rx_buffer_, sizeof(rx_buffer_)),
        [self, this](std::error_code ec, size_t bytes_transferred) {
            if (ec) {
                close_session();
                if (on_close_) on_close_(self);
                return;
            }

            memory_tracker::instance().add_mqtt_bytes(bytes_transferred);

            if (!ws_handshake_done_) {
                rx_stream_buffer_.insert(rx_stream_buffer_.end(), rx_buffer_, rx_buffer_ + bytes_transferred);
                std::string req_str(reinterpret_cast<const char*>(rx_stream_buffer_.data()), rx_stream_buffer_.size());

                if (req_str.find("GET ") == 0 || req_str.find("get ") == 0) {
                    auto header_end_pos = req_str.find("\r\n\r\n");
                    if (header_end_pos != std::string::npos) {
                        std::string ws_key, ws_protocol;
                        if (websocket_codec::parse_handshake_request(req_str, ws_key, ws_protocol)) {
                            std::string accept_key = websocket_codec::compute_accept_key(ws_key);
                            std::string response = websocket_codec::generate_handshake_response(accept_key, "mqtt");

                            is_websocket_ = true;
                            ws_handshake_done_ = true;
                            LOG_INFO("WebSocket MQTT handshake accepted for peer: " + peer_ip_);

                            // Erase handshake HTTP request from stream buffer
                            rx_stream_buffer_.erase(rx_stream_buffer_.begin(), rx_stream_buffer_.begin() + header_end_pos + 4);

                            auto resp_buf = std::make_shared<std::string>(std::move(response));
                            asio::async_write(socket_, asio::buffer(*resp_buf), [self, resp_buf](std::error_code w_ec, size_t) {
                                if (w_ec) {
                                    LOG_WARN("Failed to write WS handshake response: " + w_ec.message());
                                }
                            });
                        } else {
                            close_session();
                            if (on_close_) on_close_(self);
                            return;
                        }
                    } else {
                        // Wait for full HTTP headers
                        do_read();
                        return;
                    }
                } else {
                    // Regular TCP MQTT connection
                    ws_handshake_done_ = true;
                    is_websocket_ = false;
                    process_packet(rx_stream_buffer_.data(), rx_stream_buffer_.size());
                    rx_stream_buffer_.clear();
                    do_read();
                    return;
                }
            } else if (is_websocket_ && bytes_transferred > 0) {
                rx_stream_buffer_.insert(rx_stream_buffer_.end(), rx_buffer_, rx_buffer_ + bytes_transferred);
            }

            if (is_websocket_) {
                while (!rx_stream_buffer_.empty()) {
                    ws_frame frame;
                    size_t consumed = websocket_codec::decode_frame(rx_stream_buffer_.data(), rx_stream_buffer_.size(), frame);
                    if (consumed == 0) {
                        break; // Wait for full WS frame
                    }

                    rx_stream_buffer_.erase(rx_stream_buffer_.begin(), rx_stream_buffer_.begin() + consumed);

                    if (frame.opcode == 0x01 || frame.opcode == 0x02) { // Text or Binary frame
                        process_packet(reinterpret_cast<const uint8_t*>(frame.payload.data()), frame.payload.size());
                    } else if (frame.opcode == 0x08) { // Close frame
                        LOG_INFO("WebSocket close frame received from peer: " + peer_ip_);
                        close_session();
                        if (on_close_) on_close_(self);
                        return;
                    } else if (frame.opcode == 0x09) { // Ping frame
                        auto pong = websocket_codec::encode_frame(reinterpret_cast<const uint8_t*>(frame.payload.data()), frame.payload.size(), 0x0A);
                        auto p_buf = std::make_shared<std::vector<uint8_t>>(std::move(pong));
                        asio::async_write(socket_, asio::buffer(*p_buf), [self, p_buf](std::error_code, size_t){});
                    }
                }
            } else {
                process_packet(rx_buffer_, bytes_transferred);
            }

            do_read();
        });
}

void mqtt_session::process_packet(const uint8_t* data, size_t len) {
    if (len == 0) return;
    uint8_t pkt_type = data[0] & 0xF0;

    switch (pkt_type) {
        case static_cast<uint8_t>(mqtt_type::connect):
            handle_connect(data, len);
            break;
        case static_cast<uint8_t>(mqtt_type::publish):
            handle_publish(data, len);
            break;
        case static_cast<uint8_t>(mqtt_type::subscribe):
            handle_subscribe(data, len);
            break;
        case static_cast<uint8_t>(mqtt_type::pingreq):
            handle_pingreq();
            break;
        case static_cast<uint8_t>(mqtt_type::disconnect):
            handle_disconnect();
            break;
        default:
            break;
    }
}

void mqtt_session::handle_connect(const uint8_t* data, size_t len) {
    mqtt_connect conn;
    if (!mqtt_codec::decode_connect(data, len, conn)) {
        send_raw(mqtt_codec::encode_connack(0x01)); // Refused: unacceptable protocol
        return;
    }

    client_id_ = conn.client_id;
    username_ = conn.username;

    // Authentication: Username format is "{role}_{random_suffix}" or "{role}"
    // Password must equal access_key_ + username_
    std::string expected_password = access_key_ + username_;
    if (conn.password != expected_password && !access_key_.empty()) {
        LOG_WARN("MQTT auth failed for user: " + username_);
        send_raw(mqtt_codec::encode_connack(0x04)); // Bad username or password
        return;
    }

    size_t underscore_pos = username_.find('_');
    if (underscore_pos != std::string::npos) {
        role_ = username_.substr(0, underscore_pos);
    } else {
        role_ = username_;
    }

    authenticated_ = true;
    LOG_INFO("MQTT client connected: " + client_id_ + " (role: " + role_ + ")");
    send_raw(mqtt_codec::encode_connack(0x00)); // Accepted
}

void mqtt_session::handle_publish(const uint8_t* data, size_t len) {
    if (!authenticated_) return;

    mqtt_publish pub;
    if (!mqtt_codec::decode_publish(data, len, pub)) return;

    if (pub.qos == 1) {
        send_raw(mqtt_codec::encode_puback(pub.packet_id));
    }

    if (pub.topic == "apr/node/meta") {
        try {
            auto j = nlohmann::json::parse(pub.payload);
            std::string role = j.value("role", role_);
            std::vector<std::string> workers;
            if (j.contains("workers") && j["workers"].is_array()) {
                workers = j["workers"].get<std::vector<std::string>>();
            }
            std::optional<endpoint_info> ep;
            if (j.contains("endpoint") && !j["endpoint"].is_null()) {
                ep = j["endpoint"].get<endpoint_info>();
            }

            auto node = registry_.register_or_update_node(role, workers, ep, peer_ip_, node_id_);
            node_id_ = node.id;
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("Failed to parse apr/node/meta payload: ") + e.what());
        }
    }
}

void mqtt_session::handle_subscribe(const uint8_t* data, size_t len) {
    if (!authenticated_) return;

    mqtt_subscribe sub;
    if (!mqtt_codec::decode_subscribe(data, len, sub)) return;

    std::vector<uint8_t> return_codes;
    {
        std::lock_guard<std::mutex> lock(sub_mutex_);
        for (const auto& t : sub.topics) {
            subscriptions_.push_back(t);
            return_codes.push_back(0x00); // Granted QoS 0
            LOG_INFO("Client " + client_id_ + " subscribed to " + t);
        }
    }

    send_raw(mqtt_codec::encode_suback(sub.packet_id, return_codes));
}

void mqtt_session::handle_pingreq() {
    send_raw(mqtt_codec::encode_pingresp());
}

void mqtt_session::handle_disconnect() {
    close_session();
    if (on_close_) {
        on_close_(shared_from_this());
    }
}

mqtt_server::mqtt_server(asio::io_context& io_ctx, uint16_t port, registry& reg, const std::string& access_key)
    : io_ctx_(io_ctx),
      acceptor_(io_ctx, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      registry_(reg),
      access_key_(access_key) {
    
    // Register registry callback to broadcast topology changes to subscribers of apr/{role}
    registry_.set_event_callback([this](const node_info& node) {
        nlohmann::json j = node;
        std::string topic = "apr/" + node.role;
        broadcast(topic, j.dump());
    });
}

mqtt_server::~mqtt_server() {
    stop();
}

void mqtt_server::start() {
    do_accept();
    LOG_INFO("MQTT server listening on port " + std::to_string(acceptor_.local_endpoint().port()));
}

void mqtt_server::stop() {
    std::error_code ec;
    acceptor_.close(ec);

    std::unique_lock lock(sessions_mutex_);
    sessions_.clear();
}

void mqtt_server::broadcast(const std::string& topic, const std::string& payload) {
    auto packet = mqtt_codec::encode_publish(topic, payload);
    std::shared_lock lock(sessions_mutex_);
    for (const auto& session : sessions_) {
        if (session->matches_topic(topic)) {
            session->send_raw(packet);
        }
    }
}

void mqtt_server::do_accept() {
    acceptor_.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec) {
            auto session = std::make_shared<mqtt_session>(
                std::move(socket),
                registry_,
                access_key_,
                [this](std::shared_ptr<mqtt_session> s) {
                    std::unique_lock lock(sessions_mutex_);
                    sessions_.erase(s);
                });
            {
                std::unique_lock lock(sessions_mutex_);
                sessions_.insert(session);
            }
            session->start();
        }
        if (acceptor_.is_open()) {
            do_accept();
        }
    });
}

} // namespace apr
