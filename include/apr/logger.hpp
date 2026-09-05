#ifndef APR_LOGGER_HPP
#define APR_LOGGER_HPP

#include <string>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <filesystem>
#include <fstream>

namespace apr {

enum class log_level {
    debug,
    info,
    warn,
    error
};

inline std::string to_string(log_level level) {
    switch (level) {
        case log_level::debug: return "DEBUG";
        case log_level::info:  return "INFO";
        case log_level::warn:  return "WARN";
        case log_level::error: return "ERROR";
    }
    return "INFO";
}

inline log_level log_level_from_string(const std::string& str) {
    if (str == "debug") return log_level::debug;
    if (str == "warn") return log_level::warn;
    if (str == "error") return log_level::error;
    return log_level::info;
}

struct log_message {
    log_level level;
    std::string message;
    std::string timestamp;
};

class i_logger {
public:
    virtual ~i_logger() = default;
    virtual void log(log_level level, const std::string& message) = 0;
    virtual void rotate_log_files() = 0;
};

class async_logger : public i_logger {
public:
    async_logger(bool is_standalone, const std::filesystem::path& log_path = "lightapr.log", size_t max_file_size = 5 * 1024 * 1024);
    ~async_logger() override;

    void log(log_level level, const std::string& message) override;
    void rotate_log_files() override;

private:
    void worker_loop();
    std::string current_timestamp();
    void write_entry(const log_message& msg);

    bool is_standalone_{true};
    std::filesystem::path log_path_;
    size_t max_file_size_{5 * 1024 * 1024};
    std::ofstream file_stream_;

    std::queue<log_message> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{true};
    std::thread worker_thread_;
};

class logger_registry {
public:
    static logger_registry& instance();
    void set_logger(std::shared_ptr<i_logger> logger);
    std::shared_ptr<i_logger> get();

    // Lock-free: read on every LOG_* call, so filtering doesn't add contention.
    void set_min_level(log_level lvl) { min_level_.store(lvl, std::memory_order_relaxed); }
    log_level get_min_level() const { return min_level_.load(std::memory_order_relaxed); }

private:
    std::shared_ptr<i_logger> logger_;
    std::mutex mutex_;
    std::atomic<log_level> min_level_{log_level::info};
};

// The level check runs before `msg` is evaluated, so an expensive string
// concatenation at a call site is skipped entirely when filtered out.
#define LOG_DEBUG(msg) do { if (::apr::logger_registry::instance().get_min_level() <= ::apr::log_level::debug) { if (auto l = ::apr::logger_registry::instance().get()) l->log(::apr::log_level::debug, msg); } } while(0)
#define LOG_INFO(msg)  do { if (::apr::logger_registry::instance().get_min_level() <= ::apr::log_level::info)  { if (auto l = ::apr::logger_registry::instance().get()) l->log(::apr::log_level::info, msg); } } while(0)
#define LOG_WARN(msg)  do { if (::apr::logger_registry::instance().get_min_level() <= ::apr::log_level::warn)  { if (auto l = ::apr::logger_registry::instance().get()) l->log(::apr::log_level::warn, msg); } } while(0)
#define LOG_ERROR(msg) do { if (::apr::logger_registry::instance().get_min_level() <= ::apr::log_level::error) { if (auto l = ::apr::logger_registry::instance().get()) l->log(::apr::log_level::error, msg); } } while(0)

} // namespace apr

#endif // APR_LOGGER_HPP
