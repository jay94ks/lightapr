#ifndef APR_HTTP_SERVER_HPP
#define APR_HTTP_SERVER_HPP

#include "apr/registry.hpp"
#include "apr/logger.hpp"
#include "apr/connection_guard.hpp"
#include "apr/stream_accumulator.hpp"
#include "apr/tcp_session.hpp"
#include "http_parsing.hpp"
#include <asio.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>

namespace apr {

inline constexpr size_t k_default_http_session_buffer_bytes = 256 * 1024;
inline constexpr size_t k_default_max_requests_per_connection = 10000;

class http_session : public tcp_session_base<http_session, 4096> {
public:
    friend class tcp_session_base<http_session, 4096>;

    http_session(asio::ip::tcp::socket socket,
                 registry& reg,
                 const std::string& cell_id,
                 std::chrono::steady_clock::time_point start_time,
                 connection_guard& conn_guard,
                 bool monitor_enabled,
                 bool tester_enabled,
                 size_t max_buffer_bytes = k_default_http_session_buffer_bytes,
                 std::chrono::seconds idle_timeout = std::chrono::seconds(k_default_session_idle_timeout_sec),
                 size_t max_requests_per_connection = k_default_max_requests_per_connection);

    void start();

private:
    // tcp_session_base hooks
    void on_bytes_read(const uint8_t* data, size_t n);
    void on_before_close();
    void on_session_closed() {}

    // Parses and handles the next complete buffered request, if any; writes
    // the response and either processes the next already-buffered
    // (pipelined) request, waits for more bytes (keep-alive), or closes the
    // connection - never issues a second overlapping write.
    void try_process_next_request();
    http_response route_request(const http_request& req);

    registry& registry_;
    std::string cell_id_;
    std::chrono::steady_clock::time_point start_time_;
    connection_guard& conn_guard_;
    bool monitor_enabled_;
    bool tester_enabled_;
    size_t max_requests_per_connection_;
    size_t request_count_{0};

    stream_accumulator<std::string> request_acc_;
    std::string response_data_;
};

class http_server {
public:
    http_server(asio::io_context& io_ctx,
                uint16_t port,
                registry& reg,
                const std::string& cell_id,
                connection_guard& conn_guard,
                bool monitor_enabled = false,
                bool tester_enabled = false,
                size_t max_session_buffer_bytes = k_default_http_session_buffer_bytes,
                std::chrono::seconds session_idle_timeout = std::chrono::seconds(k_default_session_idle_timeout_sec),
                size_t max_requests_per_connection = k_default_max_requests_per_connection);
    ~http_server();

    void start();
    void stop();

private:
    void do_accept();

    asio::io_context& io_ctx_;
    asio::ip::tcp::acceptor acceptor_;
    registry& registry_;
    std::string cell_id_;
    std::chrono::steady_clock::time_point start_time_;
    connection_guard& conn_guard_;
    bool monitor_enabled_;
    bool tester_enabled_;
    size_t max_session_buffer_bytes_;
    std::chrono::seconds session_idle_timeout_;
    size_t max_requests_per_connection_;
};

} // namespace apr

#endif // APR_HTTP_SERVER_HPP
