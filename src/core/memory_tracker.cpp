#include "apr/memory_tracker.hpp"
#include <algorithm>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <fstream>
#endif

namespace apr {

memory_tracker& memory_tracker::instance() {
    static memory_tracker inst;
    return inst;
}

void memory_tracker::add_registry_bytes(int64_t bytes) {
    registry_bytes_ += bytes;
}

void memory_tracker::add_mqtt_rx_bytes(int64_t bytes) {
    mqtt_rx_bytes_ += bytes;
}

void memory_tracker::add_mqtt_tx_bytes(int64_t bytes) {
    mqtt_tx_bytes_ += bytes;
}

void memory_tracker::add_http_rx_bytes(int64_t bytes) {
    http_rx_bytes_ += bytes;
}

void memory_tracker::add_http_tx_bytes(int64_t bytes) {
    http_tx_bytes_ += bytes;
}

void memory_tracker::add_other_bytes(int64_t bytes) {
    other_bytes_ += bytes;
}

uint64_t memory_tracker::get_process_rss_kb() const {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<uint64_t>(pmc.WorkingSetSize / 1024);
    }
    return 0;
#else
    long pages = 0;
    long page_size = sysconf(_SC_PAGESIZE);
    std::ifstream statm("/proc/self/statm");
    if (statm >> pages >> pages) { // second value is RSS in pages
        return static_cast<uint64_t>((pages * page_size) / 1024);
    }
    return 0;
#endif
}

memory_stats memory_tracker::get_stats() const {
    memory_stats stats;
    stats.total_kb = get_process_rss_kb();
    stats.registry_kb = static_cast<uint64_t>(std::max<int64_t>(0, registry_bytes_.load() / 1024));
    stats.mqtt_rx_kb = static_cast<uint64_t>(std::max<int64_t>(0, mqtt_rx_bytes_.load() / 1024));
    stats.mqtt_tx_kb = static_cast<uint64_t>(std::max<int64_t>(0, mqtt_tx_bytes_.load() / 1024));
    stats.http_rx_kb = static_cast<uint64_t>(std::max<int64_t>(0, http_rx_bytes_.load() / 1024));
    stats.http_tx_kb = static_cast<uint64_t>(std::max<int64_t>(0, http_tx_bytes_.load() / 1024));
    stats.other_kb = static_cast<uint64_t>(std::max<int64_t>(0, other_bytes_.load() / 1024));
    return stats;
}

} // namespace apr
