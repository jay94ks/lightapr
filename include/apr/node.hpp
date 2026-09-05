#ifndef APR_NODE_HPP
#define APR_NODE_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <nlohmann/json.hpp>

namespace apr {

enum class node_status {
    ok,
    grace,
    erased
};

inline std::string to_string(node_status status) {
    switch (status) {
        case node_status::ok: return "OK";
        case node_status::grace: return "GRACE";
        case node_status::erased: return "ERASED";
    }
    return "OK";
}

inline node_status node_status_from_string(const std::string& str) {
    if (str == "GRACE") return node_status::grace;
    if (str == "ERASED") return node_status::erased;
    return node_status::ok;
}

struct endpoint_info {
    std::string addr;
    uint16_t port{80};
    std::string scheme{"http"};
};

inline void to_json(nlohmann::json& j, const endpoint_info& ep) {
    j = nlohmann::json{
        {"addr", ep.addr},
        {"port", ep.port},
        {"scheme", ep.scheme}
    };
}

inline void from_json(const nlohmann::json& j, endpoint_info& ep) {
    if (j.contains("addr") && j["addr"].is_string()) {
        ep.addr = j["addr"].get<std::string>();
    }
    if (j.contains("port")) {
        if (j["port"].is_number()) {
            ep.port = j["port"].get<uint16_t>();
        } else if (j["port"].is_string()) {
            ep.port = static_cast<uint16_t>(std::stoul(j["port"].get<std::string>()));
        }
    } else {
        ep.port = 80;
    }
    if (j.contains("scheme") && j["scheme"].is_string()) {
        ep.scheme = j["scheme"].get<std::string>();
    } else {
        ep.scheme = "http";
    }
}

struct node_info {
    std::string id;
    std::string role;
    std::vector<std::string> workers;
    std::optional<endpoint_info> endpoint;
    node_status status{node_status::ok};
    std::int64_t added_at{0};
    std::int64_t active_at{0};
    std::optional<std::int64_t> expires_in;
};

inline void to_json(nlohmann::json& j, const node_info& node) {
    j = nlohmann::json{
        {"id", node.id},
        {"role", node.role},
        {"workers", node.workers},
        {"endpoint", node.endpoint.has_value() ? nlohmann::json(node.endpoint.value()) : nullptr},
        {"status", to_string(node.status)},
        {"added_at", node.added_at},
        {"active_at", node.active_at}
    };
    if (node.expires_in.has_value()) {
        j["expires_in"] = node.expires_in.value();
    } else {
        j["expires_in"] = nullptr;
    }
}

inline void from_json(const nlohmann::json& j, node_info& node) {
    if (j.contains("id") && j["id"].is_string()) node.id = j["id"].get<std::string>();
    if (j.contains("role") && j["role"].is_string()) node.role = j["role"].get<std::string>();
    if (j.contains("workers") && j["workers"].is_array()) node.workers = j["workers"].get<std::vector<std::string>>();
    if (j.contains("endpoint") && !j["endpoint"].is_null()) {
        node.endpoint = j["endpoint"].get<endpoint_info>();
    } else {
        node.endpoint = std::nullopt;
    }
    if (j.contains("status") && j["status"].is_string()) {
        node.status = node_status_from_string(j["status"].get<std::string>());
    }
    if (j.contains("added_at") && j["added_at"].is_number()) node.added_at = j["added_at"].get<std::int64_t>();
    if (j.contains("active_at") && j["active_at"].is_number()) node.active_at = j["active_at"].get<std::int64_t>();
    if (j.contains("expires_in") && j["expires_in"].is_number()) {
        node.expires_in = j["expires_in"].get<std::int64_t>();
    } else {
        node.expires_in = std::nullopt;
    }
}

} // namespace apr

#endif // APR_NODE_HPP
