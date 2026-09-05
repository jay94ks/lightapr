#include "apr/logger.hpp"
#include "apr/memory_tracker.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace apr {

logger_registry& logger_registry::instance() {
    static logger_registry inst;
    return inst;
}

void logger_registry::set_logger(std::shared_ptr<i_logger> logger) {
    std::lock_guard<std::mutex> lock(mutex_);
    logger_ = std::move(logger);
}

std::shared_ptr<i_logger> logger_registry::get() {
    std::lock_guard<std::mutex> lock(mutex_);
    return logger_;
}

async_logger::async_logger(bool is_standalone, const std::filesystem::path& log_path, size_t max_file_size)
    : is_standalone_(is_standalone), log_path_(log_path), max_file_size_(max_file_size) {
    if (!is_standalone_) {
        file_stream_.open(log_path_, std::ios::out | std::ios::app);
    }
    worker_thread_ = std::thread(&async_logger::worker_loop, this);
}

async_logger::~async_logger() {
    running_ = false;
    cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

std::string async_logger::current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    std::tm buf{};
#if defined(_WIN32)
    localtime_s(&buf, &in_time_t);
#else
    localtime_r(&in_time_t, &buf);
#endif
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

namespace {
// Rough footprint of one queued log_message, for the "other" memory_tracker
// bucket - a live gauge of how much the (unbounded) async log queue is
// currently holding, useful as an early warning if log volume outpaces the
// worker thread's drain rate.
int64_t estimate_log_message_bytes(const log_message& msg) {
    return static_cast<int64_t>(sizeof(log_message) + msg.message.size() + msg.timestamp.size());
}
} // namespace

void async_logger::log(log_level level, const std::string& message) {
    log_message msg{level, message, current_timestamp()};
    memory_tracker::instance().add_other_bytes(estimate_log_message_bytes(msg));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(msg);
    }
    cv_.notify_one();
}

void async_logger::rotate_log_files() {
    if (is_standalone_ || !file_stream_.is_open()) return;

    file_stream_.close();
    auto backup_path = log_path_;
    backup_path.replace_extension(".old.log");
    
    std::error_code ec;
    std::filesystem::rename(log_path_, backup_path, ec);
    file_stream_.open(log_path_, std::ios::out | std::ios::trunc);
}

void async_logger::write_entry(const log_message& msg) {
    std::string formatted = "[" + msg.timestamp + "] [" + to_string(msg.level) + "] " + msg.message;
    if (is_standalone_) {
        if (msg.level == log_level::error || msg.level == log_level::warn) {
            std::cerr << formatted << std::endl;
        } else {
            std::cout << formatted << std::endl;
        }
    } else {
        if (file_stream_.is_open()) {
            file_stream_ << formatted << "\n";
            file_stream_.flush();
            if (std::filesystem::exists(log_path_) && std::filesystem::file_size(log_path_) >= max_file_size_) {
                rotate_log_files();
            }
        }
    }
}

void async_logger::worker_loop() {
    while (running_ || !queue_.empty()) {
        std::queue<log_message> local;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !queue_.empty() || !running_;
            });
            std::swap(local, queue_);
        }

        while (!local.empty()) {
            memory_tracker::instance().add_other_bytes(-estimate_log_message_bytes(local.front()));
            write_entry(local.front());
            local.pop();
        }
    }
}

} // namespace apr
