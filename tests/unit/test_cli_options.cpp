#include "apr/cli_options.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

// Builds an argv-shaped array from string args (argv[0] is a dummy program name).
struct fake_argv {
    std::vector<std::string> storage;
    std::vector<char*> ptrs;

    explicit fake_argv(std::vector<std::string> args) : storage(std::move(args)) {
        storage.insert(storage.begin(), "lightapr");
        for (auto& s : storage) ptrs.push_back(s.data());
    }
    int argc() const { return static_cast<int>(ptrs.size()); }
    char** argv() { return ptrs.data(); }
};

std::filesystem::path write_temp_config(const std::string& name, const std::string& content) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream f(path, std::ios::trunc);
    f << content;
    f.close();
    return path;
}

} // namespace

void test_cli_options_config_only() {
    auto path = write_temp_config("lightapr_test_config_only.json", R"({
        "cell_id": "from-config",
        "threads": 4,
        "mqtt_port": 1990
    })");

    fake_argv args({"--config", path.string()});
    auto opts = apr::cli_options::parse(args.argc(), args.argv());

    assert(opts.cell_id == "from-config");
    assert(opts.threads == 4);
    assert(opts.mqtt_port == 1990);
    // Fields absent from the config file keep their compiled-in defaults.
    assert(opts.http_port == 8080);

    std::filesystem::remove(path);
    std::cout << "[PASS] test_cli_options_config_only" << std::endl;
}

void test_cli_options_malformed_config_is_ignored() {
    auto path = write_temp_config("lightapr_test_malformed.json", "{ not valid json ");

    fake_argv args({"--config", path.string()});
    auto opts = apr::cli_options::parse(args.argc(), args.argv());

    // A parse failure must leave the defaults untouched, not crash or half-apply.
    assert(opts.cell_id == "default_cell");
    assert(opts.mqtt_port == 1883);

    std::filesystem::remove(path);
    std::cout << "[PASS] test_cli_options_malformed_config_is_ignored" << std::endl;
}

void test_cli_options_type_mismatch_keeps_default() {
    auto path = write_temp_config("lightapr_test_type_mismatch.json", R"({
        "mqtt_port": "not-a-number",
        "cell_id": 12345
    })");

    fake_argv args({"--config", path.string()});
    auto opts = apr::cli_options::parse(args.argc(), args.argv());

    // Type-mismatched fields are skipped, not coerced or crashed on.
    assert(opts.mqtt_port == 1883);
    assert(opts.cell_id == "default_cell");

    std::filesystem::remove(path);
    std::cout << "[PASS] test_cli_options_type_mismatch_keeps_default" << std::endl;
}

void test_cli_options_cli_overrides_config() {
    auto path = write_temp_config("lightapr_test_override.json", R"({
        "cell_id": "from-config",
        "threads": 4
    })");

    // --cell-id appears AFTER --config in argv.
    fake_argv args({"--config", path.string(), "--cell-id", "from-cli"});
    auto opts = apr::cli_options::parse(args.argc(), args.argv());

    assert(opts.cell_id == "from-cli"); // CLI wins
    assert(opts.threads == 4);          // untouched field still comes from config

    std::filesystem::remove(path);
    std::cout << "[PASS] test_cli_options_cli_overrides_config" << std::endl;
}

void test_cli_options_precedence_is_argv_order_independent() {
    auto path = write_temp_config("lightapr_test_order.json", R"({
        "cell_id": "from-config"
    })");

    // --cell-id appears BEFORE --config this time; CLI must still win.
    fake_argv args({"--cell-id", "from-cli", "--config", path.string()});
    auto opts = apr::cli_options::parse(args.argc(), args.argv());

    assert(opts.cell_id == "from-cli");

    std::filesystem::remove(path);
    std::cout << "[PASS] test_cli_options_precedence_is_argv_order_independent" << std::endl;
}

void test_cli_options_defaults_with_no_args() {
    fake_argv args({});
    auto opts = apr::cli_options::parse(args.argc(), args.argv());

    assert(opts.cell_id == "default_cell");
    assert(opts.mqtt_port == 1883);
    assert(opts.threads == 0);
    assert(opts.log_level == "info");

    std::cout << "[PASS] test_cli_options_defaults_with_no_args" << std::endl;
}

void run_cli_options_tests() {
    test_cli_options_config_only();
    test_cli_options_malformed_config_is_ignored();
    test_cli_options_type_mismatch_keeps_default();
    test_cli_options_cli_overrides_config();
    test_cli_options_precedence_is_argv_order_independent();
    test_cli_options_defaults_with_no_args();
}
