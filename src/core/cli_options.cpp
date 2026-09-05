#include "apr/cli_options.hpp"
#include <iostream>
#include <string_view>

namespace apr {

cli_options cli_options::parse(int argc, char* argv[]) {
    cli_options opts;

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
        } else if ((arg == "-l" || arg == "--log-file") && i + 1 < argc) {
            opts.log_file = argv[++i];
        }
    }

    return opts;
}

} // namespace apr
