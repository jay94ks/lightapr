#include "http_server.hpp"
#include "apr/memory_tracker.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

namespace apr {

http_session::http_session(asio::ip::tcp::socket socket,
                           registry& reg,
                           const std::string& cell_id,
                           std::chrono::steady_clock::time_point start_time,
                           connection_guard& conn_guard,
                           size_t max_buffer_bytes,
                           std::chrono::seconds idle_timeout,
                           size_t max_requests_per_connection)
    : tcp_session_base<http_session, 4096>(std::move(socket), idle_timeout, max_buffer_bytes),
      registry_(reg),
      cell_id_(cell_id),
      start_time_(start_time),
      conn_guard_(conn_guard),
      max_requests_per_connection_(max_requests_per_connection) {}

void http_session::on_before_close() {
    conn_guard_.release(peer_ip_);
}

void http_session::start() {
    start_read_loop();
}

void http_session::on_bytes_read(const uint8_t* data, size_t n) {
    request_acc_.append(reinterpret_cast<const char*>(data), n);

    if (exceeds_buffer_cap(request_acc_.total_capacity())) {
        LOG_WARN("HTTP session exceeded max buffer size; closing peer");
        close_session();
        return;
    }

    try_process_next_request();
}

void http_session::try_process_next_request() {
    std::string_view buffered(request_acc_.data(), request_acc_.size());
    auto boundary = find_request_boundary(buffered);
    if (!boundary) {
        start_read_loop(); // wait for more bytes to complete the request
        return;
    }

    std::string raw_request(buffered.substr(0, *boundary));
    request_acc_.consume(*boundary);
    request_acc_.compact_if_needed();

    http_request req = parse_raw_http(raw_request);
    http_response res = route_request(req);
    bool keep_alive = req.keep_alive;

    ++request_count_;
    if (max_requests_per_connection_ > 0 && request_count_ >= max_requests_per_connection_) {
        // Cap reached: this is the last request served on this connection.
        // A well-behaved client sees a normal response with Connection:
        // close and reconnects for further requests, which then goes back
        // through connection_guard's per-IP rate limit - bounding worst-case
        // request throughput a single kept-alive connection can sustain.
        keep_alive = false;
    }

    response_data_.clear();
    response_data_.reserve(res.body.size() + 128);
    response_data_ += "HTTP/1.1 ";
    response_data_ += std::to_string(res.status_code);
    response_data_ += ' ';
    response_data_ += res.status_text;
    response_data_ += "\r\nContent-Type: ";
    response_data_ += res.content_type;
    response_data_ += "\r\nContent-Length: ";
    response_data_ += std::to_string(res.body.size());
    response_data_ += keep_alive ? "\r\nConnection: keep-alive\r\n\r\n" : "\r\nConnection: close\r\n\r\n";
    response_data_ += res.body;

    auto self = shared_from_this();
    asio::async_write(socket_, asio::buffer(response_data_),
        asio::bind_executor(strand_, [self, this, keep_alive](std::error_code ec, size_t /*bytes*/) {
            if (ec || !keep_alive) {
                std::error_code ignore_ec;
                socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
                close_session();
                return;
            }
            // Either process an already-buffered (pipelined) next request, or
            // wait for more bytes - never issues a second overlapping write.
            try_process_next_request();
        }));
}

http_response http_session::route_request(const http_request& req) {
    http_response res;

    if (req.path == "/healthz") {
        auto now = std::chrono::steady_clock::now();
        uint64_t uptime_sec = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();

        nlohmann::json j = {
            {"status", "UP"},
            {"cell_id", cell_id_},
            {"uptime", uptime_sec}
        };
        res.body = j.dump();
        return res;
    }

    if (req.path == "/status") {
        auto stats = registry_.get_stats();
        auto mem = memory_tracker::instance().get_stats();

        nlohmann::json j = {
            {"nodes", {
                {"total", stats.total_nodes},
                {"alive", stats.alive_nodes},
                {"grace", stats.grace_nodes}
            }},
            {"memory", mem},
            {"roles", stats.roles},
            {"workers", stats.workers}
        };
        res.body = j.dump();
        return res;
    }

    if (req.path == "/registry") {
        size_t page = 1;
        size_t count = 50;
        std::string role_filter;
        std::string worker_filter;

        if (req.query_params.count("page")) {
            page = std::stoul(req.query_params.at("page"));
        }
        if (req.query_params.count("count")) {
            count = std::stoul(req.query_params.at("count"));
        }
        if (req.query_params.count("role")) {
            role_filter = req.query_params.at("role");
        }
        if (req.query_params.count("worker")) {
            worker_filter = req.query_params.at("worker");
        }

        auto [total, nodes] = registry_.query_registry(page, count, role_filter, worker_filter);

        nlohmann::json node_array = nlohmann::json::array();
        for (const auto& n : nodes) {
            node_array.push_back(nlohmann::json(n));
        }

        nlohmann::json j = {
            {"total", total},
            {"nodes", node_array}
        };
        res.body = j.dump();
        return res;
    }

    static const std::string kRegistryPrefix = "/registry/";
    if (req.path.rfind(kRegistryPrefix, 0) == 0) {
        std::string id = req.path.substr(kRegistryPrefix.size());
        auto node_opt = registry_.get_node(id);
        if (node_opt.has_value()) {
            nlohmann::json j = node_opt.value();
            res.body = j.dump();
        } else {
            res.status_code = 404;
            res.status_text = "Not Found";
            nlohmann::json j = {{"error", "Node not found"}};
            res.body = j.dump();
        }
        return res;
    }

    if (req.path == "/resolve") {
        std::string role;
        std::string worker;
        if (req.query_params.count("role")) {
            role = req.query_params.at("role");
        }
        if (req.query_params.count("worker")) {
            worker = req.query_params.at("worker");
        }

        if (role.empty()) {
            res.status_code = 400;
            res.status_text = "Bad Request";
            nlohmann::json j = {{"error", "Missing required query parameter: role"}};
            res.body = j.dump();
            return res;
        }

        auto node_opt = registry_.resolve_node(role, worker);
        if (node_opt.has_value()) {
            const auto& n = node_opt.value();
            nlohmann::json j = {
                {"id", n.id},
                {"endpoint", n.endpoint.has_value() ? nlohmann::json(n.endpoint.value()) : nullptr}
            };
            res.body = j.dump();
        } else {
            res.status_code = 404;
            res.status_text = "Not Found";
            nlohmann::json j = {{"error", "No available node found"}};
            res.body = j.dump();
        }
        return res;
    }

    res.status_code = 404;
    res.status_text = "Not Found";
    nlohmann::json j = {{"error", "Route not found"}};
    res.body = j.dump();
    return res;
}

http_server::http_server(asio::io_context& io_ctx,
                         uint16_t port,
                         registry& reg,
                         const std::string& cell_id,
                         connection_guard& conn_guard,
                         size_t max_session_buffer_bytes,
                         std::chrono::seconds session_idle_timeout,
                         size_t max_requests_per_connection)
    : io_ctx_(io_ctx),
      acceptor_(io_ctx, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      registry_(reg),
      cell_id_(cell_id),
      start_time_(std::chrono::steady_clock::now()),
      conn_guard_(conn_guard),
      max_session_buffer_bytes_(max_session_buffer_bytes),
      session_idle_timeout_(session_idle_timeout),
      max_requests_per_connection_(max_requests_per_connection) {}

http_server::~http_server() {
    stop();
}

void http_server::start() {
    do_accept();
    LOG_INFO("HTTP server listening on port " + std::to_string(acceptor_.local_endpoint().port()));
}

void http_server::stop() {
    std::error_code ec;
    acceptor_.close(ec);
}

void http_server::do_accept() {
    acceptor_.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec) {
            std::error_code ep_ec;
            auto remote = socket.remote_endpoint(ep_ec);
            std::string peer_ip = ep_ec ? std::string() : remote.address().to_string();

            if (!conn_guard_.try_acquire(peer_ip)) {
                LOG_DEBUG("Rejected HTTP connection from " + peer_ip + ": connection limit exceeded");
                std::error_code close_ec;
                socket.close(close_ec);
            } else {
                auto session = std::make_shared<http_session>(std::move(socket), registry_, cell_id_, start_time_,
                                                               conn_guard_, max_session_buffer_bytes_,
                                                               session_idle_timeout_, max_requests_per_connection_);
                session->start();
            }
        }
        if (acceptor_.is_open()) {
            do_accept();
        }
    });
}

} // namespace apr
