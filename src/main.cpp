#include "apr/cli_options.hpp"
#include "apr/connection_guard.hpp"
#include "apr/logger.hpp"
#include "apr/platform.hpp"
#include "apr/registry.hpp"
#include "mqtt/mqtt_server.hpp"
#include "http/http_server.hpp"
#include <asio.hpp>
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    auto opts = apr::cli_options::parse(argc, argv);

    // Check environment variables if not set via CLI
    if (const char* env_cell = std::getenv("CELL_ID")) {
        opts.cell_id = env_cell;
    }
    if (const char* env_key = std::getenv("ACCESS_KEY")) {
        opts.access_key = env_key;
    }
    if (opts.threads == 0) {
        if (const char* env_threads = std::getenv("WORKER_THREADS")) {
            opts.threads = static_cast<size_t>(std::stoul(env_threads));
        } else if (const char* env_threads = std::getenv("THREADS")) {
            opts.threads = static_cast<size_t>(std::stoul(env_threads));
        }
    }

    // Initialize logger
    auto logger = std::make_shared<apr::async_logger>(opts.standalone, opts.log_file);
    apr::logger_registry::instance().set_logger(logger);
    apr::logger_registry::instance().set_min_level(apr::log_level_from_string(opts.log_level));

    LOG_INFO("Starting LightAPR (Cell ID: " + opts.cell_id + ", Mode: " + (opts.standalone ? "Standalone" : "Daemon") + ")");

    auto platform = apr::create_platform();

    if (!opts.standalone) {
        if (!platform->acquire_single_instance_lock("lightapr")) {
            LOG_ERROR("Another instance of LightAPR is already running. Exiting.");
            return 1;
        }
        platform->initialize_daemon("lightapr");
    }

    asio::io_context io_ctx;

    // Graceful shutdown wiring
    platform->setup_signal_handlers([&io_ctx]() {
        LOG_INFO("Received shutdown signal. Stopping server...");
        io_ctx.stop();
    });

    apr::registry reg;

    // Timer for periodic grace period expiration sweep
    auto sweep_timer = std::make_shared<asio::steady_timer>(io_ctx, std::chrono::seconds(5));
    std::function<void(const std::error_code&)> do_sweep;
    do_sweep = [sweep_timer, &reg, &do_sweep](const std::error_code& ec) {
        if (!ec) {
            reg.sweep_expired_nodes();
            sweep_timer->expires_after(std::chrono::seconds(5));
            sweep_timer->async_wait([sweep_timer, &reg, &do_sweep](const std::error_code& err) {
                if (!err) {
                    do_sweep(err);
                }
            });
        }
    };
    sweep_timer->async_wait(do_sweep);

    auto idle_timeout = std::chrono::seconds(opts.session_idle_timeout_sec);

    // Shared across every listener (MQTT TCP, MQTT WebSocket, HTTP) so a
    // single attacker IP - or a flood spread across ports - is bounded by
    // one process-wide connection ceiling and per-IP limits, not a separate
    // budget per port.
    apr::connection_limits conn_limits;
    conn_limits.max_total_connections = opts.max_connections;
    conn_limits.max_connections_per_ip = opts.max_connections_per_ip;
    conn_limits.max_new_connections_per_ip = opts.max_new_connections_per_ip;
    conn_limits.rate_window = std::chrono::seconds(opts.connection_rate_window_sec);
    apr::connection_guard conn_guard(conn_limits);

    try {
        apr::mqtt_server mqtt_srv(io_ctx, opts.mqtt_port, reg, opts.access_key, conn_guard,
                                  opts.max_session_buffer_bytes, idle_timeout);
        mqtt_srv.start();

        std::unique_ptr<apr::mqtt_server> mqtt_ws_srv;
        if (opts.ws_port > 0 && opts.ws_port != opts.mqtt_port) {
            mqtt_ws_srv = std::make_unique<apr::mqtt_server>(io_ctx, opts.ws_port, reg, opts.access_key, conn_guard,
                                                              opts.max_session_buffer_bytes, idle_timeout);
            mqtt_ws_srv->start();
            LOG_INFO("Dedicated WebSocket MQTT server listening on port " + std::to_string(opts.ws_port));
        }

        apr::http_server http_srv(io_ctx, opts.http_port, reg, opts.cell_id, conn_guard,
                                   opts.max_session_buffer_bytes, idle_timeout, opts.max_requests_per_connection);
        http_srv.start();

        size_t num_threads = opts.threads;
        if (num_threads == 0) {
            num_threads = std::max<size_t>(2, std::thread::hardware_concurrency());
        }

        LOG_INFO("LightAPR initialized successfully. Running I/O event loop with " + std::to_string(num_threads) + " worker threads...");

        auto work_guard = asio::make_work_guard(io_ctx);
        std::vector<std::thread> worker_threads;
        worker_threads.reserve(num_threads - 1);

        for (size_t i = 0; i < num_threads - 1; ++i) {
            worker_threads.emplace_back([&io_ctx, i]() {
                try {
                    io_ctx.run();
                } catch (const std::exception& e) {
                    LOG_ERROR("Worker thread " + std::to_string(i + 1) + " error: " + e.what());
                }
            });
        }

        io_ctx.run();

        for (auto& t : worker_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Fatal error in main event loop: ") + e.what());
        return 1;
    }

    LOG_INFO("LightAPR shut down gracefully.");
    return 0;
}
