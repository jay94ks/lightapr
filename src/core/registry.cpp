#include "apr/registry.hpp"
#include "apr/memory_tracker.hpp"
#include "apr/logger.hpp"
#include <random>
#include <algorithm>
#include <sstream>

namespace apr {

static int64_t get_current_unix_timestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

registry::registry() = default;
registry::~registry() = default;

uint64_t registry::add_event_callback(node_event_callback cb) {
    std::unique_lock lock(mutex_);
    uint64_t token = event_cb_id_counter_++;
    event_cbs_.emplace_back(token, std::move(cb));
    return token;
}

void registry::remove_event_callback(uint64_t token) {
    std::unique_lock lock(mutex_);
    event_cbs_.erase(
        std::remove_if(event_cbs_.begin(), event_cbs_.end(),
                        [token](const auto& entry) { return entry.first == token; }),
        event_cbs_.end());
}

namespace {
std::vector<node_event_callback> snapshot_callbacks(
    const std::vector<std::pair<uint64_t, node_event_callback>>& event_cbs) {
    std::vector<node_event_callback> cbs;
    cbs.reserve(event_cbs.size());
    for (const auto& [token, cb] : event_cbs) {
        cbs.push_back(cb);
    }
    return cbs;
}
} // namespace

std::string registry::generate_node_id() {
    uint64_t counter = id_counter_++;
    auto now = get_current_unix_timestamp();
    std::stringstream ss;
    ss << "node-" << now << "-" << counter;
    return ss.str();
}

node_info registry::register_or_update_node(const std::string& role,
                                              const std::vector<std::string>& workers,
                                              const std::optional<endpoint_info>& ep,
                                              const std::string& peer_ip,
                                              const std::string& existing_id) {
    std::unique_lock lock(mutex_);
    int64_t now = get_current_unix_timestamp();

    std::string node_id = existing_id;
    if (node_id.empty()) {
        node_id = generate_node_id();
    }

    std::optional<endpoint_info> final_ep = ep;
    if (final_ep.has_value() && final_ep->addr.empty()) {
        final_ep->addr = peer_ip;
    }

    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        it->second.role = role;
        it->second.workers = workers;
        it->second.endpoint = final_ep;
        it->second.status = node_status::ok;
        it->second.active_at = now;
        it->second.expires_in.reset();
    } else {
        node_info node;
        node.id = node_id;
        node.role = role;
        node.workers = workers;
        node.endpoint = final_ep;
        node.status = node_status::ok;
        node.added_at = now;
        node.active_at = now;
        node.expires_in.reset();
        it = nodes_.emplace(node_id, std::move(node)).first;

        // Estimate memory usage for metadata tracking
        memory_tracker::instance().add_meta_bytes(sizeof(node_info) + role.size() + node_id.size());
    }

    // Snapshot for use after the lock is released (returned to the caller and
    // passed to observers below).
    node_info node = it->second;
    std::vector<node_event_callback> cbs = snapshot_callbacks(event_cbs_);
    lock.unlock();

    LOG_INFO("Registered node: " + node.id + " (role: " + node.role + ")");

    for (const auto& cb : cbs) {
        if (cb) cb(node);
    }

    return node;
}

bool registry::mark_node_grace(const std::string& node_id) {
    std::unique_lock lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return false;

    int64_t now = get_current_unix_timestamp();
    it->second.status = node_status::grace;
    it->second.active_at = now;
    it->second.expires_in = 180; // 3-minute grace period

    node_info updated_node = it->second;
    std::vector<node_event_callback> cbs = snapshot_callbacks(event_cbs_);
    lock.unlock();

    LOG_INFO("Node entered grace period: " + node_id);

    for (const auto& cb : cbs) {
        if (cb) cb(updated_node);
    }
    return true;
}

bool registry::restore_node_active(const std::string& node_id) {
    std::unique_lock lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return false;

    int64_t now = get_current_unix_timestamp();
    it->second.status = node_status::ok;
    it->second.active_at = now;
    it->second.expires_in.reset();

    node_info updated_node = it->second;
    std::vector<node_event_callback> cbs = snapshot_callbacks(event_cbs_);
    lock.unlock();

    LOG_INFO("Node restored to OK: " + node_id);

    for (const auto& cb : cbs) {
        if (cb) cb(updated_node);
    }
    return true;
}

bool registry::remove_node_permanently(const std::string& node_id) {
    std::unique_lock lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return false;

    node_info erased_node = it->second;
    erased_node.status = node_status::erased;
    erased_node.expires_in = 0;

    nodes_.erase(it);
    memory_tracker::instance().add_meta_bytes(-static_cast<int64_t>(sizeof(node_info) + erased_node.role.size() + erased_node.id.size()));

    std::vector<node_event_callback> cbs = snapshot_callbacks(event_cbs_);
    lock.unlock();

    LOG_INFO("Permanently removed node: " + node_id);

    for (const auto& cb : cbs) {
        if (cb) cb(erased_node);
    }
    return true;
}

std::optional<node_info> registry::get_node(const std::string& node_id) const {
    std::shared_lock lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        auto node = it->second;
        if (node.status == node_status::grace && node.expires_in.has_value()) {
            int64_t now = get_current_unix_timestamp();
            int64_t elapsed = now - node.active_at;
            int64_t remaining = 180 - elapsed;
            node.expires_in = (remaining > 0) ? remaining : 0;
        }
        return node;
    }
    return std::nullopt;
}

registry_stats registry::get_stats() const {
    std::shared_lock lock(mutex_);
    registry_stats stats;
    stats.total_nodes = nodes_.size();

    for (const auto& [id, node] : nodes_) {
        if (node.status == node_status::ok) {
            stats.alive_nodes++;
        } else if (node.status == node_status::grace) {
            stats.grace_nodes++;
        }

        stats.roles[node.role]++;
        for (const auto& w : node.workers) {
            stats.workers[w]++;
        }
    }
    return stats;
}

std::pair<size_t, std::vector<node_info>> registry::query_registry(
    size_t page, size_t count,
    const std::string& role_filter,
    const std::string& worker_filter) const {
    
    std::shared_lock lock(mutex_);
    int64_t now = get_current_unix_timestamp();

    std::vector<node_info> matched;
    for (const auto& [id, node] : nodes_) {
        if (node.status == node_status::erased) continue;

        if (!role_filter.empty() && node.role != role_filter) {
            continue;
        }

        if (!worker_filter.empty()) {
            bool found_worker = false;
            for (const auto& w : node.workers) {
                if (w == worker_filter) {
                    found_worker = true;
                    break;
                }
            }
            if (!found_worker) continue;
        }

        node_info n = node;
        if (n.status == node_status::grace) {
            int64_t elapsed = now - n.active_at;
            int64_t remaining = 180 - elapsed;
            n.expires_in = (remaining > 0) ? remaining : 0;
        }
        matched.push_back(std::move(n));
    }

    size_t total = matched.size();
    if (page < 1) page = 1;
    size_t start_idx = (page - 1) * count;

    if (start_idx >= total) {
        return {total, {}};
    }

    size_t end_idx = std::min(start_idx + count, total);
    std::vector<node_info> paged(matched.begin() + start_idx, matched.begin() + end_idx);
    return {total, paged};
}

std::optional<node_info> registry::resolve_node(const std::string& role, const std::string& worker) {
    std::shared_lock lock(mutex_);

    std::vector<node_info> candidates;
    for (const auto& [id, node] : nodes_) {
        if (node.status != node_status::ok) continue;
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

    if (candidates.empty()) {
        return std::nullopt;
    }

    std::string key = role + ":" + worker;
    std::lock_guard<std::mutex> rr_lock(rr_mutex_);
    size_t idx = rr_indices_[key] % candidates.size();
    rr_indices_[key] = (idx + 1) % candidates.size();

    return candidates[idx];
}

void registry::sweep_expired_nodes() {
    std::vector<std::string> to_remove;
    int64_t now = get_current_unix_timestamp();

    {
        std::shared_lock lock(mutex_);
        for (const auto& [id, node] : nodes_) {
            if (node.status == node_status::grace) {
                if (now - node.active_at >= 180) { // 3 minutes grace period expired
                    to_remove.push_back(id);
                }
            }
        }
    }

    for (const auto& id : to_remove) {
        remove_node_permanently(id);
    }
}

} // namespace apr
