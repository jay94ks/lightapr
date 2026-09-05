#ifndef APR_HTTP_SERVER_HPP
#define APR_HTTP_SERVER_HPP

#include "apr/registry.hpp"
#include "apr/logger.hpp"
#include <asio.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>

namespace apr {

struct http_request {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> query_params;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct http_response {
    int status_code{200};
    std::string status_text{"OK"};
    std::string content_type{"application/json"};
    std::string body;
};

class http_session : public std::enable_shared_from_this<http_session> {
public:
    http_session(asio::ip::tcp::socket socket,
                 registry& reg,
                 const std::string& cell_id,
                 std::chrono::steady_clock::time_point start_time);

    void start();

private:
    void do_read();
    void handle_request(const std::string& raw_request);
    http_response route_request(const http_request& req);

    asio::ip::tcp::socket socket_;
    asio::strand<asio::any_io_executor> strand_;
    registry& registry_;
    std::string cell_id_;
    std::chrono::steady_clock::time_point start_time_;

    char rx_buffer_[4096];
    std::string request_data_;
    std::string response_data_;
};

class http_server {
public:
    http_server(asio::io_context& io_ctx,
                uint16_t port,
                registry& reg,
                const std::string& cell_id);
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
};

} // namespace apr

#endif // APR_HTTP_SERVER_HPP
