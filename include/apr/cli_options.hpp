#ifndef APR_CLI_OPTIONS_HPP
#define APR_CLI_OPTIONS_HPP

#include <string>
#include <cstdint>

namespace apr {

struct cli_options {
    bool standalone{false};
    std::string cell_id{"default_cell"};
    std::string access_key{"lightapr_secret_key"};
    uint16_t mqtt_port{1883};
    uint16_t ws_port{8083};
    uint16_t http_port{8080};
    size_t threads{0}; // 0 = auto-detect from hardware_concurrency
    std::string log_file{"lightapr.log"};
    std::string log_level{"info"}; // debug|info|warn|error
    size_t session_idle_timeout_sec{90};
    size_t max_session_buffer_bytes{262144}; // 256 KiB

    // DDoS / high-load hardening. All 0 = unlimited (not recommended in
    // production). Connection limits apply across MQTT (TCP+WS) and HTTP
    // combined, since a single attacker can target any listening port.
    size_t max_connections{10000};
    size_t max_connections_per_ip{100};
    size_t max_new_connections_per_ip{20};
    size_t connection_rate_window_sec{10};
    size_t max_requests_per_connection{10000}; // HTTP keep-alive only; 0 = unlimited

    static cli_options parse(int argc, char* argv[]);

    // Loads options from a JSON config file, overriding any field present in the file.
    // Fields absent from the file are left untouched in `opts`. Returns false on I/O
    // or parse failure.
    static bool load_from_file(const std::string& path, cli_options& opts);
};

} // namespace apr

#endif // APR_CLI_OPTIONS_HPP
