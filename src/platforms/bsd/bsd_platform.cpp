#include "apr/platform.hpp"
#include "apr/logger.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#include <memory>

namespace apr {

static std::function<void()> g_bsd_shutdown_cb;

static void bsd_signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        if (g_bsd_shutdown_cb) {
            g_bsd_shutdown_cb();
        }
    }
}

class bsd_platform : public i_platform {
public:
    bsd_platform() = default;
    ~bsd_platform() override {
        release_single_instance_lock();
    }

    bool initialize_daemon(const std::string& service_name) override {
        if (daemon(0, 0) < 0) {
            return false;
        }
        LOG_INFO("Daemonized BSD process: " + service_name);
        return true;
    }

    bool acquire_single_instance_lock(const std::string& app_name) override {
        pid_file_path_ = "/var/run/" + app_name + ".pid";
        pid_fd_ = open(pid_file_path_.c_str(), O_RDWR | O_CREAT, 0640);
        if (pid_fd_ < 0) {
            pid_file_path_ = "/tmp/" + app_name + ".pid";
            pid_fd_ = open(pid_file_path_.c_str(), O_RDWR | O_CREAT, 0640);
            if (pid_fd_ < 0) return false;
        }

        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;

        if (fcntl(pid_fd_, F_SETLK, &fl) < 0) {
            close(pid_fd_);
            pid_fd_ = -1;
            return false;
        }

        ftruncate(pid_fd_, 0);
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%ld\n", (long)getpid());
        write(pid_fd_, buf, len);
        return true;
    }

    void release_single_instance_lock() override {
        if (pid_fd_ >= 0) {
            close(pid_fd_);
            pid_fd_ = -1;
            unlink(pid_file_path_.c_str());
        }
    }

    void setup_signal_handlers(std::function<void()> shutdown_cb) override {
        g_bsd_shutdown_cb = shutdown_cb;
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = bsd_signal_handler;
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
    }

private:
    int pid_fd_{-1};
    std::string pid_file_path_;
};

} // namespace apr
