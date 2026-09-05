#ifndef APR_CONNECTION_GUARD_HPP
#define APR_CONNECTION_GUARD_HPP

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace apr {

struct connection_limits {
    size_t max_total_connections{0};     // 0 = unlimited
    size_t max_connections_per_ip{0};    // 0 = unlimited
    size_t max_new_connections_per_ip{0}; // 0 = unlimited (rate limit)
    std::chrono::seconds rate_window{std::chrono::seconds(10)};
};

// Tracks total and per-source-IP connection counts/rates across every
// listening socket (MQTT plain TCP, MQTT WebSocket, HTTP), so a single
// client IP can't exhaust server resources by opening unbounded connections
// on any one port, and the server as a whole has a hard ceiling on live
// connections regardless of source port or IP. Meant to be shared (one
// instance) across all listeners so limits apply to the process as a whole.
//
// Thread-safe: called concurrently from the multi-threaded io_context's
// accept handlers, one call per accepted socket before any other work is
// done with it, so a rejected connection costs only a mutex-protected map
// lookup plus an immediate socket close - no session object, no read.
class connection_guard {
public:
    explicit connection_guard(connection_limits limits);

    // Call once per accepted socket, before constructing a session for it.
    // Returns true if the connection is allowed - the caller MUST then call
    // release(ip) exactly once when that connection's session ends (however
    // it ends). Returns false if a limit was exceeded, in which case the
    // caller should close the raw socket immediately and must NOT call
    // release() for it (nothing was acquired).
    bool try_acquire(const std::string& ip);
    void release(const std::string& ip);

    size_t total_connections() const;

private:
    connection_limits limits_;
    mutable std::mutex mutex_;
    size_t total_{0};
    std::unordered_map<std::string, size_t> per_ip_count_;
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> per_ip_recent_;
};

} // namespace apr

#endif // APR_CONNECTION_GUARD_HPP
