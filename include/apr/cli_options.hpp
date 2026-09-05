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
    size_t threads{0};
    std::string log_file{"lightapr.log"};

    static cli_options parse(int argc, char* argv[]);

    // Loads options from a JSON config file, overriding any field present in the file.
    // Fields absent from the file are left untouched in `opts`. Returns false on I/O
    // or parse failure.
    static bool load_from_file(const std::string& path, cli_options& opts);
};

} // namespace apr

#endif // APR_CLI_OPTIONS_HPP
