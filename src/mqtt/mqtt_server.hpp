#ifndef APR_MQTT_SERVER_HPP
#define APR_MQTT_SERVER_HPP

#include "apr/registry.hpp"
#include "apr/logger.hpp"
#include <asio.hpp>
#include <memory>
#include <set>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>

namespace apr {

class mqtt_session : public std::enable_shared_from_this<mqtt_session> {
public:
    using close_callback = std::function<void(std::shared_ptr<mqtt_session>)>;

    mqtt_session(asio::ip::tcp::socket socket, registry& reg, const std::string& access_key, close_callback on_close);
    ~mqtt_session();

    void start();
    void send_raw(const std::vector<uint8_t>& data);
    
    bool matches_topic(const std::string& topic) const;
    const std::string& get_node_id() const { return node_id_; }

private:
    void do_read();
    void process_packet(const uint8_t* data, size_t len);

    void handle_connect(const uint8_t* data, size_t len);
    void handle_publish(const uint8_t* data, size_t len);
    void handle_subscribe(const uint8_t* data, size_t len);
    void handle_pingreq();
    void handle_disconnect();
    void close_session();

    asio::ip::tcp::socket socket_;
    registry& registry_;
    std::string access_key_;
    close_callback on_close_;

    uint8_t rx_buffer_[4096];
    std::vector<uint8_t> rx_stream_buffer_;

    bool is_websocket_{false};
    bool ws_handshake_done_{false};
    bool authenticated_{false};
    bool closed_{false};
    std::string client_id_;
    std::string username_;
    std::string role_;
    std::string node_id_;
    std::string peer_ip_;

    mutable std::mutex sub_mutex_;
    std::vector<std::string> subscriptions_;
};

class mqtt_server {
public:
    mqtt_server(asio::io_context& io_ctx, uint16_t port, registry& reg, const std::string& access_key);
    ~mqtt_server();

    void start();
    void stop();

    void broadcast(const std::string& topic, const std::string& payload);

private:
    void do_accept();

    asio::io_context& io_ctx_;
    asio::ip::tcp::acceptor acceptor_;
    registry& registry_;
    std::string access_key_;

    mutable std::shared_mutex sessions_mutex_;
    std::set<std::shared_ptr<mqtt_session>> sessions_;
};

} // namespace apr

#endif // APR_MQTT_SERVER_HPP
