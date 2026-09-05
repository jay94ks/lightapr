#include "apr/cli_options.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string_view>

namespace apr {

bool cli_options::load_from_file(const std::string& path, cli_options& opts) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[LightAPR] Failed to open config file: " << path << std::endl;
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        std::cerr << "[LightAPR] Failed to parse config file '" << path << "': " << e.what() << std::endl;
        return false;
    }

    if (j.contains("standalone") && j["standalone"].is_boolean()) {
        opts.standalone = j["standalone"].get<bool>();
    }
    if (j.contains("cell_id") && j["cell_id"].is_string()) {
        opts.cell_id = j["cell_id"].get<std::string>();
    }
    if (j.contains("access_key") && j["access_key"].is_string()) {
        opts.access_key = j["access_key"].get<std::string>();
    }
    if (j.contains("mqtt_port") && j["mqtt_port"].is_number_unsigned()) {
        opts.mqtt_port = j["mqtt_port"].get<uint16_t>();
    }
    if (j.contains("ws_port") && j["ws_port"].is_number_unsigned()) {
        opts.ws_port = j["ws_port"].get<uint16_t>();
    }
    if (j.contains("http_port") && j["http_port"].is_number_unsigned()) {
        opts.http_port = j["http_port"].get<uint16_t>();
    }
    if (j.contains("threads") && j["threads"].is_number_unsigned()) {
        opts.threads = j["threads"].get<size_t>();
    }
    if (j.contains("log_file") && j["log_file"].is_string()) {
        opts.log_file = j["log_file"].get<std::string>();
    }
    if (j.contains("log_level") && j["log_level"].is_string()) {
        opts.log_level = j["log_level"].get<std::string>();
    }
    if (j.contains("session_idle_timeout_sec") && j["session_idle_timeout_sec"].is_number_unsigned()) {
        opts.session_idle_timeout_sec = j["session_idle_timeout_sec"].get<size_t>();
    }
    if (j.contains("max_session_buffer_bytes") && j["max_session_buffer_bytes"].is_number_unsigned()) {
        opts.max_session_buffer_bytes = j["max_session_buffer_bytes"].get<size_t>();
    }
    if (j.contains("max_connections") && j["max_connections"].is_number_unsigned()) {
        opts.max_connections = j["max_connections"].get<size_t>();
    }
    if (j.contains("max_connections_per_ip") && j["max_connections_per_ip"].is_number_unsigned()) {
        opts.max_connections_per_ip = j["max_connections_per_ip"].get<size_t>();
    }
    if (j.contains("max_new_connections_per_ip") && j["max_new_connections_per_ip"].is_number_unsigned()) {
        opts.max_new_connections_per_ip = j["max_new_connections_per_ip"].get<size_t>();
    }
    if (j.contains("connection_rate_window_sec") && j["connection_rate_window_sec"].is_number_unsigned()) {
        opts.connection_rate_window_sec = j["connection_rate_window_sec"].get<size_t>();
    }
    if (j.contains("max_requests_per_connection") && j["max_requests_per_connection"].is_number_unsigned()) {
        opts.max_requests_per_connection = j["max_requests_per_connection"].get<size_t>();
    }

    return true;
}

namespace {
// Tracks which fields were explicitly set via CLI flags during a single pass
// over argv, so they can be re-applied on top of a config file loaded from a
// --config flag encountered anywhere else in argv (order-independent).
struct cli_overrides {
    cli_options values;
    bool standalone{false};
    bool cell_id{false};
    bool access_key{false};
    bool mqtt_port{false};
    bool ws_port{false};
    bool http_port{false};
    bool threads{false};
    bool log_file{false};
    bool log_level{false};
    bool session_idle_timeout_sec{false};
    bool max_session_buffer_bytes{false};
    bool max_connections{false};
    bool max_connections_per_ip{false};
    bool max_new_connections_per_ip{false};
    bool connection_rate_window_sec{false};
    bool max_requests_per_connection{false};
};

void apply_overrides(const cli_overrides& ov, cli_options& opts) {
    if (ov.standalone) opts.standalone = ov.values.standalone;
    if (ov.cell_id) opts.cell_id = ov.values.cell_id;
    if (ov.access_key) opts.access_key = ov.values.access_key;
    if (ov.mqtt_port) opts.mqtt_port = ov.values.mqtt_port;
    if (ov.ws_port) opts.ws_port = ov.values.ws_port;
    if (ov.http_port) opts.http_port = ov.values.http_port;
    if (ov.threads) opts.threads = ov.values.threads;
    if (ov.log_file) opts.log_file = ov.values.log_file;
    if (ov.log_level) opts.log_level = ov.values.log_level;
    if (ov.session_idle_timeout_sec) opts.session_idle_timeout_sec = ov.values.session_idle_timeout_sec;
    if (ov.max_session_buffer_bytes) opts.max_session_buffer_bytes = ov.values.max_session_buffer_bytes;
    if (ov.max_connections) opts.max_connections = ov.values.max_connections;
    if (ov.max_connections_per_ip) opts.max_connections_per_ip = ov.values.max_connections_per_ip;
    if (ov.max_new_connections_per_ip) opts.max_new_connections_per_ip = ov.values.max_new_connections_per_ip;
    if (ov.connection_rate_window_sec) opts.connection_rate_window_sec = ov.values.connection_rate_window_sec;
    if (ov.max_requests_per_connection) opts.max_requests_per_connection = ov.values.max_requests_per_connection;
}
} // namespace

cli_options cli_options::parse(int argc, char* argv[]) {
    cli_overrides ov;
    std::string config_path;
    bool has_config = false;

    // Single pass over argv. `--config`'s path is only remembered here (not
    // loaded yet) so that CLI flags always win regardless of where --config
    // appears in argv: the config file is applied first below, then every
    // flag explicitly set here is re-applied on top of it.
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "-s" || arg == "--standalone") {
            ov.values.standalone = true;
            ov.standalone = true;
        } else if ((arg == "-c" || arg == "--cell-id") && i + 1 < argc) {
            ov.values.cell_id = argv[++i];
            ov.cell_id = true;
        } else if ((arg == "-k" || arg == "--access-key") && i + 1 < argc) {
            ov.values.access_key = argv[++i];
            ov.access_key = true;
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            ov.values.mqtt_port = static_cast<uint16_t>(std::stoul(argv[++i]));
            ov.mqtt_port = true;
        } else if ((arg == "-w" || arg == "--ws-port") && i + 1 < argc) {
            ov.values.ws_port = static_cast<uint16_t>(std::stoul(argv[++i]));
            ov.ws_port = true;
        } else if ((arg == "-h" || arg == "--http-port") && i + 1 < argc) {
            ov.values.http_port = static_cast<uint16_t>(std::stoul(argv[++i]));
            ov.http_port = true;
        } else if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
            ov.values.threads = static_cast<size_t>(std::stoul(argv[++i]));
            ov.threads = true;
        } else if ((arg == "-l" || arg == "--log-file") && i + 1 < argc) {
            ov.values.log_file = argv[++i];
            ov.log_file = true;
        } else if ((arg == "-v" || arg == "--log-level") && i + 1 < argc) {
            ov.values.log_level = argv[++i];
            ov.log_level = true;
        } else if (arg == "--idle-timeout" && i + 1 < argc) {
            ov.values.session_idle_timeout_sec = static_cast<size_t>(std::stoul(argv[++i]));
            ov.session_idle_timeout_sec = true;
        } else if (arg == "--max-buffer-bytes" && i + 1 < argc) {
            ov.values.max_session_buffer_bytes = static_cast<size_t>(std::stoul(argv[++i]));
            ov.max_session_buffer_bytes = true;
        } else if (arg == "--max-connections" && i + 1 < argc) {
            ov.values.max_connections = static_cast<size_t>(std::stoul(argv[++i]));
            ov.max_connections = true;
        } else if (arg == "--max-connections-per-ip" && i + 1 < argc) {
            ov.values.max_connections_per_ip = static_cast<size_t>(std::stoul(argv[++i]));
            ov.max_connections_per_ip = true;
        } else if (arg == "--max-new-connections-per-ip" && i + 1 < argc) {
            ov.values.max_new_connections_per_ip = static_cast<size_t>(std::stoul(argv[++i]));
            ov.max_new_connections_per_ip = true;
        } else if (arg == "--connection-rate-window-sec" && i + 1 < argc) {
            ov.values.connection_rate_window_sec = static_cast<size_t>(std::stoul(argv[++i]));
            ov.connection_rate_window_sec = true;
        } else if (arg == "--max-requests-per-connection" && i + 1 < argc) {
            ov.values.max_requests_per_connection = static_cast<size_t>(std::stoul(argv[++i]));
            ov.max_requests_per_connection = true;
        } else if ((arg == "-f" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
            has_config = true;
        }
    }

    cli_options opts;
    if (has_config) {
        load_from_file(config_path, opts);
    }
    apply_overrides(ov, opts);

    return opts;
}

} // namespace apr
