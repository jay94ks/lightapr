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
    // max_node_extra_bytes bounds the serialized size of a single worker's
    // `extra` payload (apr/node/extra and the optional `extra` on
    // apr/node/meta) - independent of any MQTT session/transport buffer
    // limit, since this is data the registry holds long-term, not a
    // transient message.
    explicit registry(size_t max_node_extra_bytes = 8192);
    ~registry();

    // Subscribe to node topology broadcast events. Returns a token usable with
    // remove_event_callback. Multiple subscribers are supported (e.g. one per
    // mqtt_server instance) - a new subscription does not replace prior ones.
    uint64_t add_event_callback(node_event_callback cb);
    void remove_event_callback(uint64_t token);

    // Node management
    node_info register_or_update_node(const std::string& role,
                                      const std::vector<std::string>& workers,
                                      const std::optional<endpoint_info>& ep,
                                      const std::string& peer_ip,
                                      const std::string& existing_id = "",
                                      const nlohmann::json& extra = nlohmann::json::object());

    bool mark_node_grace(const std::string& node_id);
    bool restore_node_active(const std::string& node_id);
    bool remove_node_permanently(const std::string& node_id);

    // Replaces the `extra` announcement for a single worker of an already-
    // registered node. `worker` must already be present in that node's
    // `workers` list (set at registration) - update_node_extra never adds a
    // new worker, only announces data for one that exists. Rejected (returns
    // false, nothing changed) if the node/worker is unknown or the
    // serialized `extra` exceeds max_node_extra_bytes. On success, fires the
    // same node_event_callback as every other mutation, so it rides the
    // existing apr/{role} broadcast to every apr/+ subscriber.
    bool update_node_extra(const std::string& node_id, const std::string& worker, const nlohmann::json& extra);

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
    // Sets node.extra[worker] = value if worker is in node.workers and the
    // serialized size is within max_node_extra_bytes_, tracking the byte
    // delta via memory_tracker::add_extra_bytes(). Returns false (no change)
    // otherwise. Caller must hold mutex_ for writing.
    bool try_set_extra_locked(node_info& node, const std::string& worker, const nlohmann::json& value);
    // Drops any node.extra entries whose key is no longer in node.workers
    // (e.g. a re-registration that stopped listing that worker), releasing
    // their tracked bytes. Caller must hold mutex_ for writing.
    void prune_stale_extra_locked(node_info& node);

    size_t max_node_extra_bytes_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, node_info> nodes_; // node_id -> node_info
    
    // Round-robin index per filter key
    std::mutex rr_mutex_;
    std::unordered_map<std::string, size_t> rr_indices_;

    std::vector<std::pair<uint64_t, node_event_callback>> event_cbs_;
    std::atomic<uint64_t> event_cb_id_counter_{1};

    std::atomic<uint64_t> id_counter_{1};
};

} // namespace apr

#endif // APR_REGISTRY_HPP
