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

static std::function<void()> g_posix_shutdown_cb;

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        if (g_posix_shutdown_cb) {
            g_posix_shutdown_cb();
        }
    }
}

class posix_platform : public i_platform {
public:
    posix_platform() = default;
    ~posix_platform() override {
        release_single_instance_lock();
    }

    bool initialize_daemon(const std::string& service_name) override {
        pid_t pid = fork();
        if (pid < 0) return false;
        if (pid > 0) _exit(0);

        if (setsid() < 0) return false;

        signal(SIGCHLD, SIG_IGN);
        signal(SIGHUP, SIG_IGN);

        pid = fork();
        if (pid < 0) return false;
        if (pid > 0) _exit(0);

        umask(0);
        chdir("/");

        int x;
        for (x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
            close(x);
        }

        int fd = open("/dev/null", O_RDWR);
        if (fd != -1) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }

        LOG_INFO("Daemonized POSIX process: " + service_name);
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
        g_posix_shutdown_cb = shutdown_cb;
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = signal_handler;
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
    }

private:
    int pid_fd_{-1};
    std::string pid_file_path_;
};

std::unique_ptr<i_platform> create_platform() {
    return std::make_unique<posix_platform>();
}

} // namespace apr
