#ifndef APR_MQTT_TOPIC_HPP
#define APR_MQTT_TOPIC_HPP

#include <string>

namespace apr {

// MQTT topic-filter matching ('#' multi-level and '+' single-level wildcards).
namespace mqtt_topic {

inline bool matches(const std::string& pattern, const std::string& topic) {
    if (pattern == "#") return true;
    if (pattern == topic) return true;

    size_t p_idx = 0, t_idx = 0;
    while (p_idx < pattern.size() && t_idx < topic.size()) {
        if (pattern[p_idx] == '#') {
            return true;
        }
        if (pattern[p_idx] == '+') {
            while (p_idx < pattern.size() && pattern[p_idx] != '/') p_idx++;
            while (t_idx < topic.size() && topic[t_idx] != '/') t_idx++;
            if (p_idx < pattern.size() && pattern[p_idx] == '/') p_idx++;
            if (t_idx < topic.size() && topic[t_idx] == '/') t_idx++;
            continue;
        }
        if (pattern[p_idx] != topic[t_idx]) {
            return false;
        }
        p_idx++;
        t_idx++;
    }

    if (p_idx == pattern.size() && t_idx == topic.size()) return true;
    if (p_idx < pattern.size() && pattern[p_idx] == '#' && p_idx == pattern.size() - 1) return true;
    return false;
}

} // namespace mqtt_topic
} // namespace apr

#endif // APR_MQTT_TOPIC_HPP
