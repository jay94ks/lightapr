#ifndef APR_MEMORY_TRACKER_HPP
#define APR_MEMORY_TRACKER_HPP

#include <cstdint>
#include <atomic>
#include <nlohmann/json.hpp>

namespace apr {

// registry_kb and other_kb are live gauges (incremented on allocation,
// decremented on release - net-zero when nothing is outstanding). The
// *_rx_kb/*_tx_kb fields are cumulative traffic counters (bytes moved over
// the process lifetime) and never decrease.
struct memory_stats {
    uint64_t total_kb{0};    // OS-reported process RSS
    uint64_t registry_kb{0}; // live: node registry metadata footprint
    uint64_t mqtt_rx_kb{0};  // cumulative: MQTT bytes received
    uint64_t mqtt_tx_kb{0};  // cumulative: MQTT bytes sent
    uint64_t http_rx_kb{0};  // cumulative: HTTP bytes received
    uint64_t http_tx_kb{0};  // cumulative: HTTP bytes sent
    uint64_t other_kb{0};    // live: misc tracked allocations (e.g. logger queue)
};

inline void to_json(nlohmann::json& j, const memory_stats& stats) {
    j = nlohmann::json{
        {"total", stats.total_kb},
        {"registry", stats.registry_kb},
        {"mqtt_rx", stats.mqtt_rx_kb},
        {"mqtt_tx", stats.mqtt_tx_kb},
        {"http_rx", stats.http_rx_kb},
        {"http_tx", stats.http_tx_kb},
        {"other", stats.other_kb}
    };
}

class memory_tracker {
public:
    static memory_tracker& instance();

    void add_registry_bytes(int64_t bytes);
    void add_mqtt_rx_bytes(int64_t bytes);
    void add_mqtt_tx_bytes(int64_t bytes);
    void add_http_rx_bytes(int64_t bytes);
    void add_http_tx_bytes(int64_t bytes);
    void add_other_bytes(int64_t bytes);

    memory_stats get_stats() const;

private:
    std::atomic<int64_t> registry_bytes_{0};
    std::atomic<int64_t> mqtt_rx_bytes_{0};
    std::atomic<int64_t> mqtt_tx_bytes_{0};
    std::atomic<int64_t> http_rx_bytes_{0};
    std::atomic<int64_t> http_tx_bytes_{0};
    std::atomic<int64_t> other_bytes_{0};

    uint64_t get_process_rss_kb() const;
};

} // namespace apr

#endif // APR_MEMORY_TRACKER_HPP
