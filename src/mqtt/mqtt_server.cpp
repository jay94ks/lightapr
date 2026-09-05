#include "mqtt_server.hpp"
#include "mqtt_packet.hpp"
#include "websocket_codec.hpp"
#include "apr/memory_tracker.hpp"
#include "apr/mqtt_topic.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string_view>

namespace apr {

mqtt_session::mqtt_session(asio::ip::tcp::socket socket, registry& reg, const std::string& access_key, close_callback on_close,
                           connection_guard& conn_guard, size_t max_buffer_bytes, std::chrono::seconds idle_timeout)
    : tcp_session_base<mqtt_session, 4096>(std::move(socket), idle_timeout, max_buffer_bytes),
      registry_(reg), access_key_(access_key), on_close_(std::move(on_close)), conn_guard_(conn_guard) {
}

mqtt_session::~mqtt_session() {
    close_session();
}

void mqtt_session::on_before_close() {
    conn_guard_.release(peer_ip_);
    if (!node_id_.empty()) {
        registry_.mark_node_grace(node_id_);
    }
}

void mqtt_session::on_session_closed() {
    if (on_close_) on_close_(shared_from_this());
}

void mqtt_session::start() {
    start_read_loop();
}

void mqtt_session::send_raw(const std::vector<uint8_t>& data) {
    send_raw(std::make_shared<const std::vector<uint8_t>>(data), /*already_framed=*/false);
}

void mqtt_session::send_raw(std::shared_ptr<const std::vector<uint8_t>> data, bool already_framed) {
    auto self = shared_from_this();

    std::shared_ptr<const std::vector<uint8_t>> send_buf = data;
    if (is_websocket_ && !already_framed) {
        auto framed = websocket_codec::encode_frame(data->data(), data->size(), 0x02);
        send_buf = std::make_shared<const std::vector<uint8_t>>(std::move(framed));
    }

    memory_tracker::instance().add_mqtt_tx_bytes(send_buf->size());

    asio::async_write(socket_, asio::buffer(*send_buf), asio::bind_executor(strand_, [self, this, send_buf](std::error_code ec, size_t /*bytes*/) {
        if (ec) {
            LOG_WARN("MQTT write error: " + ec.message());
            close_session();
            on_session_closed();
        }
    }));
}

void mqtt_session::send_raw_and_close(const std::vector<uint8_t>& data) {
    closing_ = true;
    auto self = shared_from_this();

    std::shared_ptr<const std::vector<uint8_t>> send_buf;
    if (is_websocket_) {
        auto framed = websocket_codec::encode_frame(data.data(), data.size(), 0x02);
        send_buf = std::make_shared<const std::vector<uint8_t>>(std::move(framed));
    } else {
        send_buf = std::make_shared<const std::vector<uint8_t>>(data);
    }

    memory_tracker::instance().add_mqtt_tx_bytes(send_buf->size());

    asio::async_write(socket_, asio::buffer(*send_buf), asio::bind_executor(strand_, [self, this, send_buf](std::error_code ec, size_t /*bytes*/) {
        if (ec) {
            LOG_WARN("MQTT write error: " + ec.message());
        }
        close_session();
        on_session_closed();
    }));
}

bool mqtt_session::matches_topic(const std::string& topic) const {
    std::lock_guard<std::mutex> lock(sub_mutex_);
    for (const auto& pat : subscriptions_) {
        if (mqtt_topic::matches(pat, topic)) {
            return true;
        }
    }
    return false;
}

void mqtt_session::on_bytes_read(const uint8_t* data, size_t bytes_transferred) {
    auto self = shared_from_this(); // kept alive for async writes issued below
    memory_tracker::instance().add_mqtt_rx_bytes(bytes_transferred);

    if (!ws_handshake_done_) {
        rx_acc_.append(data, bytes_transferred);

        if (exceeds_buffer_cap(rx_acc_.total_capacity())) {
            LOG_WARN("MQTT session exceeded max buffer size before handshake completed; closing peer: " + peer_ip_);
            close_session();
            on_session_closed();
            return;
        }

        std::string_view req_view(reinterpret_cast<const char*>(rx_acc_.data()), rx_acc_.size());

        if (req_view.substr(0, 4) == "GET " || req_view.substr(0, 4) == "get ") {
            auto header_end_pos = req_view.find("\r\n\r\n");
            if (header_end_pos != std::string_view::npos) {
                std::string req_str(req_view); // websocket_codec parses a std::string
                std::string ws_key, ws_protocol;
                if (websocket_codec::parse_handshake_request(req_str, ws_key, ws_protocol)) {
                    std::string accept_key = websocket_codec::compute_accept_key(ws_key);
                    std::string response = websocket_codec::generate_handshake_response(accept_key, "mqtt");

                    is_websocket_ = true;
                    ws_handshake_done_ = true;
                    LOG_INFO("WebSocket MQTT handshake accepted for peer: " + peer_ip_);

                    // Consume the handshake HTTP request from the stream buffer
                    rx_acc_.consume(header_end_pos + 4);
                    rx_acc_.compact_if_needed();

                    auto resp_buf = std::make_shared<std::string>(std::move(response));
                    asio::async_write(socket_, asio::buffer(*resp_buf), asio::bind_executor(strand_, [self, resp_buf](std::error_code w_ec, size_t) {
                        if (w_ec) {
                            LOG_WARN("Failed to write WS handshake response: " + w_ec.message());
                        }
                    }));
                } else {
                    close_session();
                    on_session_closed();
                    return;
                }
            } else {
                // Wait for full HTTP headers
                start_read_loop();
                return;
            }
        } else {
            // Regular TCP MQTT connection
            ws_handshake_done_ = true;
            is_websocket_ = false;
            process_packet(rx_acc_.data(), rx_acc_.size());
            rx_acc_.clear();
            if (!closing_) start_read_loop();
            return;
        }
    } else if (is_websocket_ && bytes_transferred > 0) {
        rx_acc_.append(data, bytes_transferred);

        if (exceeds_buffer_cap(rx_acc_.total_capacity())) {
            LOG_WARN("MQTT websocket session exceeded max buffer size; closing peer: " + peer_ip_);
            close_session();
            on_session_closed();
            return;
        }
    }

    if (is_websocket_) {
        while (!closing_ && !rx_acc_.empty()) {
            ws_frame frame;
            size_t consumed = websocket_codec::decode_frame(rx_acc_.data(), rx_acc_.size(), frame);
            if (consumed == 0) {
                break; // Wait for full WS frame
            }

            rx_acc_.consume(consumed);

            if (frame.opcode == 0x01 || frame.opcode == 0x02) { // Text or Binary frame
                process_packet(reinterpret_cast<const uint8_t*>(frame.payload.data()), frame.payload.size());
            } else if (frame.opcode == 0x08) { // Close frame
                LOG_INFO("WebSocket close frame received from peer: " + peer_ip_);
                close_session();
                on_session_closed();
                return;
            } else if (frame.opcode == 0x09) { // Ping frame
                auto pong = websocket_codec::encode_frame(reinterpret_cast<const uint8_t*>(frame.payload.data()), frame.payload.size(), 0x0A);
                auto p_buf = std::make_shared<std::vector<uint8_t>>(std::move(pong));
                asio::async_write(socket_, asio::buffer(*p_buf), asio::bind_executor(strand_, [self, p_buf](std::error_code, size_t){}));
            }
        }
        rx_acc_.compact_if_needed();
    } else {
        process_packet(data, bytes_transferred);
    }

    if (!closing_) start_read_loop();
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
        // SUBSCRIBE's fixed-header flags nibble is mandated to be 0x2 by the
        // MQTT spec, so mqtt_type::subscribe (0x82) must itself be masked with
        // 0xF0 to compare against pkt_type - unlike PUBLISH/CONNECT/etc. whose
        // enum values are already bare type nibbles. Without this mask, a
        // SUBSCRIBE packet's pkt_type (0x80) never matched 0x82 and every
        // subscription request was silently dropped.
        case (static_cast<uint8_t>(mqtt_type::subscribe) & 0xF0):
            handle_subscribe(data, len);
            break;
        case static_cast<uint8_t>(mqtt_type::pingreq):
            handle_pingreq();
            break;
        case static_cast<uint8_t>(mqtt_type::disconnect):
            handle_disconnect();
            break;
        default:
            LOG_DEBUG("Dropped unrecognized MQTT packet type: 0x" + std::to_string(pkt_type));
            break;
    }
}

void mqtt_session::handle_connect(const uint8_t* data, size_t len) {
    mqtt_connect conn;
    if (!mqtt_codec::decode_connect(data, len, conn)) {
        // Close after responding: a malformed-CONNECT client gets one chance
        // per TCP connection, forcing a reconnect (and connection_guard's
        // rate limit) instead of allowing unlimited retries on one socket.
        send_raw_and_close(mqtt_codec::encode_connack(0x01)); // Refused: unacceptable protocol
        return;
    }

    client_id_ = conn.client_id;
    username_ = conn.username;

    // Authentication: Username format is "{role}_{random_suffix}" or "{role}"
    // Password must equal access_key_ + username_
    std::string expected_password = access_key_ + username_;
    if (conn.password != expected_password && !access_key_.empty()) {
        LOG_WARN("MQTT auth failed for user: " + username_);
        // Close after responding, for the same brute-force-mitigation reason.
        send_raw_and_close(mqtt_codec::encode_connack(0x04)); // Bad username or password
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
    on_session_closed();
}

mqtt_server::mqtt_server(asio::io_context& io_ctx, uint16_t port, registry& reg, const std::string& access_key,
                         connection_guard& conn_guard,
                         size_t max_session_buffer_bytes, std::chrono::seconds session_idle_timeout)
    : io_ctx_(io_ctx),
      acceptor_(io_ctx, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      registry_(reg),
      access_key_(access_key),
      conn_guard_(conn_guard),
      max_session_buffer_bytes_(max_session_buffer_bytes),
      session_idle_timeout_(session_idle_timeout) {

    // Subscribe to broadcast topology changes to subscribers of apr/{role}.
    // Each mqtt_server instance (plain TCP + dedicated WS) keeps its own
    // subscription, since registry now supports multiple simultaneous observers.
    event_cb_token_ = registry_.add_event_callback([this](const node_info& node) {
        nlohmann::json j = node;
        std::string topic = "apr/" + node.role;
        broadcast(topic, j.dump());
    });
}

mqtt_server::~mqtt_server() {
    registry_.remove_event_callback(event_cb_token_);
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
    auto packet = std::make_shared<const std::vector<uint8_t>>(mqtt_codec::encode_publish(topic, payload));
    // Websocket subscribers all need the identical framed encoding; build it
    // once lazily instead of re-framing per subscriber.
    std::shared_ptr<const std::vector<uint8_t>> ws_framed;

    std::shared_lock lock(sessions_mutex_);
    for (const auto& session : sessions_) {
        if (!session->matches_topic(topic)) continue;

        if (session->is_websocket()) {
            if (!ws_framed) {
                ws_framed = std::make_shared<const std::vector<uint8_t>>(
                    websocket_codec::encode_frame(packet->data(), packet->size(), 0x02));
            }
            session->send_raw(ws_framed, /*already_framed=*/true);
        } else {
            session->send_raw(packet, /*already_framed=*/false);
        }
    }
}

void mqtt_server::do_accept() {
    acceptor_.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec) {
            std::error_code ep_ec;
            auto remote = socket.remote_endpoint(ep_ec);
            std::string peer_ip = ep_ec ? std::string() : remote.address().to_string();

            if (!conn_guard_.try_acquire(peer_ip)) {
                LOG_DEBUG("Rejected MQTT connection from " + peer_ip + ": connection limit exceeded");
                std::error_code close_ec;
                socket.close(close_ec);
            } else {
                auto session = std::make_shared<mqtt_session>(
                    std::move(socket),
                    registry_,
                    access_key_,
                    [this](std::shared_ptr<mqtt_session> s) {
                        std::unique_lock lock(sessions_mutex_);
                        sessions_.erase(s);
                    },
                    conn_guard_,
                    max_session_buffer_bytes_,
                    session_idle_timeout_);
                {
                    std::unique_lock lock(sessions_mutex_);
                    sessions_.insert(session);
                }
                session->start();
            }
        }
        if (acceptor_.is_open()) {
            do_accept();
        }
    });
}

} // namespace apr
