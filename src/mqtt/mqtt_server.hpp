#ifndef APR_MQTT_SERVER_HPP
#define APR_MQTT_SERVER_HPP

#include "apr/registry.hpp"
#include "apr/logger.hpp"
#include "apr/connection_guard.hpp"
#include "apr/stream_accumulator.hpp"
#include "apr/tcp_session.hpp"
#include <asio.hpp>
#include <memory>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>

namespace apr {

inline constexpr size_t k_default_max_session_buffer_bytes = 256 * 1024;

class mqtt_session : public tcp_session_base<mqtt_session, 4096> {
public:
    using close_callback = std::function<void(std::shared_ptr<mqtt_session>)>;
    friend class tcp_session_base<mqtt_session, 4096>;

    mqtt_session(asio::ip::tcp::socket socket, registry& reg, const std::string& access_key, close_callback on_close,
                 connection_guard& conn_guard,
                 size_t max_buffer_bytes = k_default_max_session_buffer_bytes,
                 std::chrono::seconds idle_timeout = std::chrono::seconds(k_default_session_idle_timeout_sec));
    ~mqtt_session();

    void start();
    void send_raw(const std::vector<uint8_t>& data);
    // Sends an already-shared buffer without copying it per subscriber. When
    // already_framed is false and this session is a websocket connection, the
    // buffer is still framed (and copied) here; callers that want to share the
    // framing cost across subscribers too should pre-frame and pass true.
    void send_raw(std::shared_ptr<const std::vector<uint8_t>> data, bool already_framed = false);
    // Queues data for send, then closes the session once it has been written
    // (or immediately, if the write fails) - used for auth/protocol failures
    // so the client sees the error response instead of an abrupt disconnect,
    // while still forcing the client to open a NEW connection to retry (so a
    // brute-force loop can't hammer a single kept-open connection; the new
    // connection attempt goes back through connection_guard's rate limit).
    void send_raw_and_close(const std::vector<uint8_t>& data);

    bool matches_topic(const std::string& topic) const;
    const std::string& get_node_id() const { return node_id_; }
    bool is_websocket() const { return is_websocket_; }

private:
    // tcp_session_base hooks
    void on_bytes_read(const uint8_t* data, size_t n);
    void on_before_close();
    void on_session_closed();

    void process_packet(const uint8_t* data, size_t len);

    void handle_connect(const uint8_t* data, size_t len);
    void handle_publish(const uint8_t* data, size_t len);
    void handle_subscribe(const uint8_t* data, size_t len);
    void handle_pingreq();
    void handle_disconnect();

    registry& registry_;
    std::string access_key_;
    close_callback on_close_;
    connection_guard& conn_guard_;

    stream_accumulator<std::vector<uint8_t>> rx_acc_;

    bool is_websocket_{false};
    bool ws_handshake_done_{false};
    bool authenticated_{false};
    // Set synchronously by send_raw_and_close() so on_bytes_read doesn't
    // re-arm a new read on a session that already has a close queued behind
    // a pending write.
    bool closing_{false};
    std::string client_id_;
    std::string username_;
    std::string role_;
    std::string node_id_;

    mutable std::mutex sub_mutex_;
    std::vector<std::string> subscriptions_;
};

class mqtt_server {
public:
    mqtt_server(asio::io_context& io_ctx, uint16_t port, registry& reg, const std::string& access_key,
                connection_guard& conn_guard,
                size_t max_session_buffer_bytes = k_default_max_session_buffer_bytes,
                std::chrono::seconds session_idle_timeout = std::chrono::seconds(k_default_session_idle_timeout_sec));
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
    connection_guard& conn_guard_;
    size_t max_session_buffer_bytes_;
    std::chrono::seconds session_idle_timeout_;
    uint64_t event_cb_token_{0};

    mutable std::shared_mutex sessions_mutex_;
    std::unordered_set<std::shared_ptr<mqtt_session>> sessions_;
};

} // namespace apr

#endif // APR_MQTT_SERVER_HPP
