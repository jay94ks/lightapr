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

    return true;
}

cli_options cli_options::parse(int argc, char* argv[]) {
    cli_options opts;

    // Pre-pass: an explicit --config file is loaded first so it replaces the
    // built-in defaults; the normal pass below still lets individual CLI flags
    // override specific fields from that file.
    for (int i = 1; i + 1 < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "-f" || arg == "--config") {
            load_from_file(argv[i + 1], opts);
            break;
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "-s" || arg == "--standalone") {
            opts.standalone = true;
        } else if ((arg == "-c" || arg == "--cell-id") && i + 1 < argc) {
            opts.cell_id = argv[++i];
        } else if ((arg == "-k" || arg == "--access-key") && i + 1 < argc) {
            opts.access_key = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            opts.mqtt_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if ((arg == "-w" || arg == "--ws-port") && i + 1 < argc) {
            opts.ws_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if ((arg == "-h" || arg == "--http-port") && i + 1 < argc) {
            opts.http_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
            opts.threads = static_cast<size_t>(std::stoul(argv[++i]));
        } else if ((arg == "-l" || arg == "--log-file") && i + 1 < argc) {
            opts.log_file = argv[++i];
        } else if ((arg == "-f" || arg == "--config") && i + 1 < argc) {
            ++i; // consumed by the pre-pass above
        }
    }

    return opts;
}

} // namespace apr
