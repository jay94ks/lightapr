#ifndef APR_REGISTRY_HPP
#define APR_REGISTRY_HPP

#include "apr/node.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <functional>
#include <atomic>
#include <chrono>

namespace apr {

using node_event_callback = std::function<void(const node_info& node)>;

struct registry_stats {
    size_t total_nodes{0};
    size_t alive_nodes{0};
    size_t grace_nodes{0};
    std::unordered_map<std::string, size_t> roles;
    std::unordered_map<std::string, size_t> workers;
};

class registry {
public:
    registry();
    ~registry();

    // Set callback for node topology broadcast events
    void set_event_callback(node_event_callback cb);

    // Node management
    node_info register_or_update_node(const std::string& role,
                                      const std::vector<std::string>& workers,
                                      const std::optional<endpoint_info>& ep,
                                      const std::string& peer_ip,
                                      const std::string& existing_id = "");

    bool mark_node_grace(const std::string& node_id);
    bool restore_node_active(const std::string& node_id);
    bool remove_node_permanently(const std::string& node_id);

    std::optional<node_info> get_node(const std::string& node_id) const;

    // Queries
    registry_stats get_stats() const;

    std::pair<size_t, std::vector<node_info>> query_registry(
        size_t page, size_t count,
        const std::string& role_filter = "",
        const std::string& worker_filter = "") const;

    std::optional<node_info> resolve_node(const std::string& role,
                                          const std::string& worker = "");

    // Clean up expired GRACE nodes (> 180 seconds)
    void sweep_expired_nodes();

private:
    std::string generate_node_id();

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, node_info> nodes_; // node_id -> node_info
    
    // Round-robin index per filter key
    std::mutex rr_mutex_;
    std::unordered_map<std::string, size_t> rr_indices_;

    node_event_callback event_cb_;

    std::atomic<uint64_t> id_counter_{1};
};

} // namespace apr

#endif // APR_REGISTRY_HPP
