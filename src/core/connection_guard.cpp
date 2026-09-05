#include "apr/connection_guard.hpp"

namespace apr {

connection_guard::connection_guard(connection_limits limits) : limits_(limits) {}

bool connection_guard::try_acquire(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (limits_.max_total_connections > 0 && total_ >= limits_.max_total_connections) {
        return false;
    }

    auto count_it = per_ip_count_.find(ip);
    size_t current_count = (count_it == per_ip_count_.end()) ? 0 : count_it->second;
    if (limits_.max_connections_per_ip > 0 && current_count >= limits_.max_connections_per_ip) {
        return false;
    }

    if (limits_.max_new_connections_per_ip > 0) {
        auto& recent = per_ip_recent_[ip];
        auto now = std::chrono::steady_clock::now();
        while (!recent.empty() && now - recent.front() > limits_.rate_window) {
            recent.pop_front();
        }
        if (recent.size() >= limits_.max_new_connections_per_ip) {
            return false;
        }
        recent.push_back(now);
    }

    ++total_;
    ++per_ip_count_[ip];
    return true;
}

void connection_guard::release(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (total_ > 0) --total_;

    auto it = per_ip_count_.find(ip);
    if (it != per_ip_count_.end()) {
        if (it->second > 0) --it->second;
        if (it->second == 0) per_ip_count_.erase(it);
    }
}

size_t connection_guard::total_connections() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_;
}

} // namespace apr
