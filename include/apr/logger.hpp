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
    i_logger* get();

private:
    std::shared_ptr<i_logger> logger_;
    std::mutex mutex_;
};

#define LOG_DEBUG(msg) do { if (auto* l = ::apr::logger_registry::instance().get()) l->log(::apr::log_level::debug, msg); } while(0)
#define LOG_INFO(msg)  do { if (auto* l = ::apr::logger_registry::instance().get()) l->log(::apr::log_level::info, msg); } while(0)
#define LOG_WARN(msg)  do { if (auto* l = ::apr::logger_registry::instance().get()) l->log(::apr::log_level::warn, msg); } while(0)
#define LOG_ERROR(msg) do { if (auto* l = ::apr::logger_registry::instance().get()) l->log(::apr::log_level::error, msg); } while(0)

} // namespace apr

#endif // APR_LOGGER_HPP
