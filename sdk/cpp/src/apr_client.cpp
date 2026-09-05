#include "apr_sdk/apr_client.hpp"
#include "apr/memory_tracker.hpp"
#include "mqtt/mqtt_packet.hpp"
#include "mqtt/websocket_codec.hpp"
#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <atomic>
#include <sstream>
#include <cstdlib>

namespace apr::sdk {

static void parse_url(const std::string& url, std::string& scheme, std::string& host, std::string& port, std::string& path) {
    scheme = "mqtt";
    host = "127.0.0.1";
    port = "1883";
    path = "/";

    auto scheme_pos = url.find("://");
    std::string rest = url;
    if (scheme_pos != std::string::npos) {
        scheme = url.substr(0, scheme_pos);
        rest = url.substr(scheme_pos + 3);
    }

    auto slash_pos = rest.find('/');
    if (slash_pos != std::string::npos) {
        path = rest.substr(slash_pos);
        rest = rest.substr(0, slash_pos);
    }

    auto colon_pos = rest.find(':');
    if (colon_pos != std::string::npos) {
        host = rest.substr(0, colon_pos);
        port = rest.substr(colon_pos + 1);
    } else if (!rest.empty()) {
        host = rest;
        if (scheme == "ws" || scheme == "wss") port = "8083";
        else port = "1883";
    }

    if (host.empty() || host == "localhost") host = "127.0.0.1";
}

class apr_client::impl {
public:
    explicit impl(const client_options& opts)
        : opts_(opts), socket_(io_ctx_) {
        
        if (const char* env_mqtt = std::getenv("APR_MQTT_URL")) opts_.mqtt_url = env_mqtt;
        if (const char* env_http = std::getenv("APR_HTTP_URL")) opts_.http_url = env_http;
        if (const char* env_role = std::getenv("APR_ROLE")) opts_.role = env_role;
        if (const char* env_key = std::getenv("APR_ACCESS_KEY")) opts_.access_key = env_key;
    }

    ~impl() {
        stop();
    }

    bool start() {
        try {
            std::string scheme, host, port, path;
            parse_url(opts_.mqtt_url, scheme, host, port, path);
            is_websocket_ = (scheme == "ws" || scheme == "wss");

            asio::ip::tcp::resolver resolver(io_ctx_);
            auto endpoints = resolver.resolve(host, port);
            asio::connect(socket_, endpoints);

            if (is_websocket_) {
                // Perform WebSocket Upgrade Handshake
                std::string req = "GET " + path + " HTTP/1.1\r\n" +
                                  "Host: " + host + ":" + port + "\r\n" +
                                  "Upgrade: websocket\r\n" +
                                  "Connection: Upgrade\r\n" +
                                  "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n" +
                                  "Sec-WebSocket-Version: 13\r\n" +
                                  "Sec-WebSocket-Protocol: mqtt\r\n\r\n";
                asio::write(socket_, asio::buffer(req));

                std::string resp;
                char buf[512];
                std::error_code ec;
                while (resp.find("\r\n\r\n") == std::string::npos) {
                    size_t n = socket_.read_some(asio::buffer(buf), ec);
                    if (ec || n == 0) break;
                    resp.append(buf, n);
                }

                if (resp.find("101") == std::string::npos) {
                    return false;
                }
            }

            send_connect();
            send_subscribe("apr/+");

            publish_metadata();
            fetch_snapshot();
            reconcile_queue();

            running_ = true;
            io_thread_ = std::thread([this]() {
                do_read();
                io_ctx_.run();
            });

            return true;
        } catch (const std::exception& e) {
            return false;
        }
    }

    void stop() {
        if (running_) {
            running_ = false;
            std::error_code ec;
            std::vector<uint8_t> disc = {0xE0, 0x00};
            asio::write(socket_, asio::buffer(disc), ec);
            socket_.close(ec);
            io_ctx_.stop();
            if (io_thread_.joinable()) {
                io_thread_.join();
            }
        }
    }

    std::optional<apr::node_info> resolve_node(const std::string& role, const std::string& worker) {
        std::shared_lock lock(nodes_mutex_);
        std::vector<apr::node_info> candidates;

        for (const auto& [id, node] : local_nodes_) {
            if (node.status != apr::node_status::ok) continue;
            if (node.role != role) continue;

            if (!worker.empty()) {
                bool found = false;
                for (const auto& w : node.workers) {
                    if (w == worker) {
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
            }
            candidates.push_back(node);
        }

        if (candidates.empty()) return std::nullopt;

        std::string key = role + ":" + worker;
        std::lock_guard<std::mutex> rr_lock(rr_mutex_);
        size_t idx = rr_indices_[key] % candidates.size();
        rr_indices_[key] = (idx + 1) % candidates.size();

        return candidates[idx];
    }

    std::vector<apr::node_info> get_local_registry() const {
        std::shared_lock lock(nodes_mutex_);
        std::vector<apr::node_info> result;
        for (const auto& [id, node] : local_nodes_) {
            result.push_back(node);
        }
        return result;
    }

    void publish_app_event(const std::string& target_role, const std::string& worker, const std::string& payload) {
        std::string topic = worker.empty() ? ("app/" + target_role) : ("app/" + target_role + "/" + worker);
        send_publish(topic, payload);
    }

    void subscribe_app_event(const std::string& target_role, const std::string& worker, app_event_callback cb) {
        std::string topic = worker.empty() ? ("app/" + target_role) : ("app/" + target_role + "/" + worker);
        {
            std::lock_guard<std::mutex> lock(app_mutex_);
            app_callbacks_[topic].push_back(cb);
        }
        send_subscribe(topic);
    }

    void set_node_status_callback(node_status_callback cb) {
        status_cb_ = std::move(cb);
    }

private:
    void send_bytes(const std::vector<uint8_t>& packet) {
        if (is_websocket_) {
            auto ws_pkt = websocket_codec::encode_frame(packet.data(), packet.size(), 0x02);
            asio::write(socket_, asio::buffer(ws_pkt));
        } else {
            asio::write(socket_, asio::buffer(packet));
        }
    }

    void send_connect() {
        std::string username = opts_.role + "_" + std::to_string(rand() % 10000);
        std::string password = opts_.access_key + username;
        std::string client_id = opts_.role + "_sdk_" + std::to_string(rand() % 10000);

        uint8_t flags = 0x02 | 0x80 | 0x40;
        size_t rem_len = 10 + (2 + client_id.size()) + (2 + username.size()) + (2 + password.size());

        std::vector<uint8_t> packet;
        packet.push_back(0x10);
        packet.push_back(static_cast<uint8_t>(rem_len));

        packet.push_back(0x00); packet.push_back(0x04);
        packet.push_back('M'); packet.push_back('Q'); packet.push_back('T'); packet.push_back('T');
        packet.push_back(0x04);
        packet.push_back(flags);
        packet.push_back(0x00); packet.push_back(0x3C);

        auto write_str = [&](const std::string& s) {
            packet.push_back(static_cast<uint8_t>((s.size() >> 8) & 0xFF));
            packet.push_back(static_cast<uint8_t>(s.size() & 0xFF));
            packet.insert(packet.end(), s.begin(), s.end());
        };

        write_str(client_id);
        write_str(username);
        write_str(password);

        send_bytes(packet);
    }

    void send_subscribe(const std::string& topic) {
        std::vector<uint8_t> body;
        body.push_back(0x00); body.push_back(0x01); // packet id 1
        body.push_back(static_cast<uint8_t>((topic.size() >> 8) & 0xFF));
        body.push_back(static_cast<uint8_t>(topic.size() & 0xFF));
        body.insert(body.end(), topic.begin(), topic.end());
        body.push_back(0x00); // QoS 0

        std::vector<uint8_t> packet;
        packet.push_back(0x82); // SUBSCRIBE
        packet.push_back(static_cast<uint8_t>(body.size()));
        packet.insert(packet.end(), body.begin(), body.end());

        send_bytes(packet);
    }

    void send_publish(const std::string& topic, const std::string& payload) {
        auto pkt = mqtt_codec::encode_publish(topic, payload);
        send_bytes(pkt);
    }

    void publish_metadata() {
        nlohmann::json j = {
            {"role", opts_.role},
            {"workers", opts_.workers},
            {"endpoint", opts_.endpoint.has_value() ? nlohmann::json(opts_.endpoint.value()) : nullptr}
        };
        send_publish("apr/node/meta", j.dump());
    }

    void fetch_snapshot() {
        try {
            // Parse host and port from http_url
            std::string url = opts_.http_url;
            std::string host = "127.0.0.1";
            std::string port_str = "8080";

            auto scheme_pos = url.find("://");
            std::string rest = (scheme_pos != std::string::npos) ? url.substr(scheme_pos + 3) : url;
            auto colon = rest.find(':');
            if (colon != std::string::npos) {
                host = rest.substr(0, colon);
                port_str = rest.substr(colon + 1);
                auto slash = port_str.find('/');
                if (slash != std::string::npos) port_str = port_str.substr(0, slash);
            }

            asio::io_context temp_io;
            asio::ip::tcp::resolver resolver(temp_io);
            asio::ip::tcp::socket http_sock(temp_io);
            asio::connect(http_sock, resolver.resolve(host, port_str));

            std::string req = "GET /registry?count=1000 HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
            asio::write(http_sock, asio::buffer(req));

            std::string resp;
            char buf[1024];
            std::error_code ec;
            while (size_t n = http_sock.read_some(asio::buffer(buf), ec)) {
                resp.append(buf, n);
            }

            auto body_pos = resp.find("\r\n\r\n");
            if (body_pos != std::string::npos) {
                std::string body = resp.substr(body_pos + 4);
                auto j = nlohmann::json::parse(body);
                if (j.contains("nodes") && j["nodes"].is_array()) {
                    std::unique_lock lock(nodes_mutex_);
                    for (const auto& nj : j["nodes"]) {
                        apr::node_info n = nj;
                        local_nodes_[n.id] = n;
                    }
                }
            }
        } catch (...) {}
    }

    void reconcile_queue() {
        std::unique_lock lock(nodes_mutex_);
        for (const auto& n : sync_queue_) {
            apply_node_event(n);
        }
        sync_queue_.clear();
        reconciled_ = true;
    }

    void apply_node_event(const apr::node_info& node) {
        if (node.status == apr::node_status::ok) {
            local_nodes_[node.id] = node;
        } else if (node.status == apr::node_status::grace) {
            auto it = local_nodes_.find(node.id);
            if (it != local_nodes_.end()) {
                it->second.status = apr::node_status::grace;
            }
        } else if (node.status == apr::node_status::erased) {
            local_nodes_.erase(node.id);
        }

        if (status_cb_) {
            status_cb_(node);
        }
    }

    void do_read() {
        auto self_buf = std::make_shared<std::vector<uint8_t>>(4096);
        socket_.async_read_some(asio::buffer(*self_buf), [this, self_buf](std::error_code ec, size_t n) {
            if (!ec && n > 0) {
                stream_buf_.insert(stream_buf_.end(), self_buf->begin(), self_buf->begin() + n);
                process_stream();
                do_read();
            }
        });
    }

    void process_stream() {
        if (is_websocket_) {
            while (!stream_buf_.empty()) {
                ws_frame frame;
                size_t consumed = websocket_codec::decode_frame(stream_buf_.data(), stream_buf_.size(), frame);
                if (consumed == 0) break;
                stream_buf_.erase(stream_buf_.begin(), stream_buf_.begin() + consumed);

                if (frame.opcode == 0x01 || frame.opcode == 0x02) {
                    process_mqtt_buffer(reinterpret_cast<const uint8_t*>(frame.payload.data()), frame.payload.size());
                }
            }
        } else {
            size_t offset = process_mqtt_buffer(stream_buf_.data(), stream_buf_.size());
            if (offset > 0) {
                stream_buf_.erase(stream_buf_.begin(), stream_buf_.begin() + offset);
            }
        }
    }

    size_t process_mqtt_buffer(const uint8_t* data, size_t size) {
        size_t offset = 0;
        while (offset < size) {
            if (size - offset < 2) break;
            uint8_t type = data[offset] & 0xF0;
            size_t rem_len = data[offset + 1];
            size_t pkt_len = 2 + rem_len;

            if (size - offset < pkt_len) break;

            if (type == 0x30) { // PUBLISH
                uint16_t topic_len = (data[offset + 2] << 8) | data[offset + 3];
                std::string topic(reinterpret_cast<const char*>(&data[offset + 4]), topic_len);
                std::string payload(reinterpret_cast<const char*>(&data[offset + 4 + topic_len]), pkt_len - (4 + topic_len));

                handle_publish(topic, payload);
            }
            offset += pkt_len;
        }
        return offset;
    }

    void handle_publish(const std::string& topic, const std::string& payload) {
        if (topic.rfind("apr/", 0) == 0) {
            try {
                auto j = nlohmann::json::parse(payload);
                apr::node_info node = j;
                std::unique_lock lock(nodes_mutex_);
                if (!reconciled_) {
                    sync_queue_.push_back(node);
                } else {
                    apply_node_event(node);
                }
            } catch (...) {}
        } else if (topic.rfind("app/", 0) == 0) {
            std::lock_guard<std::mutex> lock(app_mutex_);
            auto it = app_callbacks_.find(topic);
            if (it != app_callbacks_.end()) {
                for (const auto& cb : it->second) {
                    cb(payload, topic);
                }
            }
        }
    }

    client_options opts_;
    asio::io_context io_ctx_;
    asio::ip::tcp::socket socket_;
    std::thread io_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> reconciled_{false};
    bool is_websocket_{false};

    mutable std::shared_mutex nodes_mutex_;
    std::unordered_map<std::string, apr::node_info> local_nodes_;
    std::vector<apr::node_info> sync_queue_;

    std::mutex rr_mutex_;
    std::unordered_map<std::string, size_t> rr_indices_;

    std::mutex app_mutex_;
    std::unordered_map<std::string, std::vector<app_event_callback>> app_callbacks_;
    node_status_callback status_cb_;

    std::vector<uint8_t> stream_buf_;
};

apr_client::apr_client(const client_options& opts)
    : pimpl_(std::make_unique<impl>(opts)) {}

apr_client::~apr_client() = default;

bool apr_client::start() { return pimpl_->start(); }
void apr_client::stop() { pimpl_->stop(); }

std::optional<apr::node_info> apr_client::resolve_node(const std::string& role, const std::string& worker) {
    return pimpl_->resolve_node(role, worker);
}

std::vector<apr::node_info> apr_client::get_local_registry() const {
    return pimpl_->get_local_registry();
}

void apr_client::publish_app_event(const std::string& target_role, const std::string& worker, const std::string& payload) {
    pimpl_->publish_app_event(target_role, worker, payload);
}

void apr_client::subscribe_app_event(const std::string& target_role, const std::string& worker, app_event_callback cb) {
    pimpl_->subscribe_app_event(target_role, worker, std::move(cb));
}

void apr_client::set_node_status_callback(node_status_callback cb) {
    pimpl_->set_node_status_callback(std::move(cb));
}

} // namespace apr::sdk
