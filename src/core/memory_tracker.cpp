#include "apr/memory_tracker.hpp"

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

void memory_tracker::add_meta_bytes(int64_t bytes) {
    meta_bytes_ += bytes;
}

void memory_tracker::add_mqtt_bytes(int64_t bytes) {
    mqtt_bytes_ += bytes;
}

void memory_tracker::add_etc_bytes(int64_t bytes) {
    etc_bytes_ += bytes;
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
    stats.meta_kb = static_cast<uint64_t>(std::max<int64_t>(0, meta_bytes_.load() / 1024));
    stats.mqtt_kb = static_cast<uint64_t>(std::max<int64_t>(0, mqtt_bytes_.load() / 1024));
    stats.etc_kb = static_cast<uint64_t>(std::max<int64_t>(0, etc_bytes_.load() / 1024));
    return stats;
}

} // namespace apr
