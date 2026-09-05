#include "http_server.hpp"
#include "apr/memory_tracker.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iostream>

namespace apr {

static std::unordered_map<std::string, std::string> parse_query(const std::string& query_str) {
    std::unordered_map<std::string, std::string> result;
    std::stringstream ss(query_str);
    std::string item;
    while (std::getline(ss, item, '&')) {
        auto eq_pos = item.find('=');
        if (eq_pos != std::string::npos) {
            result[item.substr(0, eq_pos)] = item.substr(eq_pos + 1);
        } else if (!item.empty()) {
            result[item] = "";
        }
    }
    return result;
}

static http_request parse_raw_http(const std::string& raw) {
    http_request req;
    std::stringstream ss(raw);
    std::string line;

    if (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::stringstream line_ss(line);
        std::string full_path;
        line_ss >> req.method >> full_path;

        auto q_pos = full_path.find('?');
        if (q_pos != std::string::npos) {
            req.path = full_path.substr(0, q_pos);
            req.query_params = parse_query(full_path.substr(q_pos + 1));
        } else {
            req.path = full_path;
        }
    }

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break; // End of headers
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string k = line.substr(0, colon);
            std::string v = line.substr(colon + 1);
            while (!v.empty() && v.front() == ' ') v.erase(0, 1);
            req.headers[k] = v;
        }
    }

    return req;
}

http_session::http_session(asio::ip::tcp::socket socket,
                           registry& reg,
                           const std::string& cell_id,
                           std::chrono::steady_clock::time_point start_time)
    : socket_(std::move(socket)),
      registry_(reg),
      cell_id_(cell_id),
      start_time_(start_time) {}

void http_session::start() {
    do_read();
}

void http_session::do_read() {
    auto self = shared_from_this();
    socket_.async_read_some(asio::buffer(rx_buffer_, sizeof(rx_buffer_)),
        [self, this](std::error_code ec, size_t bytes_transferred) {
            if (!ec) {
                request_data_.append(rx_buffer_, bytes_transferred);
                if (request_data_.find("\r\n\r\n") != std::string::npos) {
                    handle_request(request_data_);
                } else {
                    do_read();
                }
            }
        });
}

void http_session::handle_request(const std::string& raw_request) {
    http_request req = parse_raw_http(raw_request);
    http_response res = route_request(req);

    std::stringstream ss;
    ss << "HTTP/1.1 " << res.status_code << " " << res.status_text << "\r\n";
    ss << "Content-Type: " << res.content_type << "\r\n";
    ss << "Content-Length: " << res.body.size() << "\r\n";
    ss << "Connection: close\r\n\r\n";
    ss << res.body;

    response_data_ = ss.str();
    auto self = shared_from_this();
    asio::async_write(socket_, asio::buffer(response_data_),
        [self, this](std::error_code /*ec*/, size_t /*bytes*/) {
            std::error_code ignore_ec;
            socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
        });
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
            nlohmann::json node_j = {
                {"id", n.id},
                {"role", n.role},
                {"endpoint", n.endpoint.has_value() ? nlohmann::json(n.endpoint.value()) : nullptr},
                {"added_at", n.added_at},
                {"active_at", n.active_at}
            };
            node_array.push_back(node_j);
        }

        nlohmann::json j = {
            {"total", total},
            {"nodes", node_array}
        };
        res.body = j.dump();
        return res;
    }

    if (req.path.rfind("/registry/", 0) == 0) {
        std::string id = req.path.substr(10);
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
                         const std::string& cell_id)
    : io_ctx_(io_ctx),
      acceptor_(io_ctx, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      registry_(reg),
      cell_id_(cell_id),
      start_time_(std::chrono::steady_clock::now()) {}

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
            auto session = std::make_shared<http_session>(std::move(socket), registry_, cell_id_, start_time_);
            session->start();
        }
        if (acceptor_.is_open()) {
            do_accept();
        }
    });
}

} // namespace apr
