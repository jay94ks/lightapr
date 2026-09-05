#ifndef APR_MEMORY_TRACKER_HPP
#define APR_MEMORY_TRACKER_HPP

#include <cstdint>
#include <atomic>
#include <nlohmann/json.hpp>

namespace apr {

struct memory_stats {
    uint64_t total_kb{0};
    uint64_t meta_kb{0};
    uint64_t mqtt_kb{0};
    uint64_t etc_kb{0};
};

inline void to_json(nlohmann::json& j, const memory_stats& stats) {
    j = nlohmann::json{
        {"total", stats.total_kb},
        {"meta", stats.meta_kb},
        {"mqtt", stats.mqtt_kb},
        {"etc", stats.etc_kb}
    };
}

class memory_tracker {
public:
    static memory_tracker& instance();

    void add_meta_bytes(int64_t bytes);
    void add_mqtt_bytes(int64_t bytes);
    void add_etc_bytes(int64_t bytes);

    memory_stats get_stats() const;

private:
    std::atomic<int64_t> meta_bytes_{0};
    std::atomic<int64_t> mqtt_bytes_{0};
    std::atomic<int64_t> etc_bytes_{0};

    uint64_t get_process_rss_kb() const;
};

} // namespace apr

#endif // APR_MEMORY_TRACKER_HPP
