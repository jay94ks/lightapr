#include "apr/platform.hpp"
#include "apr/logger.hpp"
#include <windows.h>
#include <iostream>
#include <memory>

namespace apr {

static std::function<void()> g_shutdown_cb;

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (g_shutdown_cb) {
                g_shutdown_cb();
            }
            return TRUE;
        default:
            return FALSE;
    }
}

class win32_platform : public i_platform {
public:
    win32_platform() = default;
    ~win32_platform() override {
        release_single_instance_lock();
    }

    bool initialize_daemon(const std::string& service_name) override {
        // Windows Service registration or background daemon setup
        LOG_INFO("Initializing Windows service platform component: " + service_name);
        return true;
    }

    bool acquire_single_instance_lock(const std::string& app_name) override {
        std::wstring wname(app_name.begin(), app_name.end());
        std::wstring mutex_name = L"Global\\" + wname;

        mutex_handle_ = CreateMutexW(NULL, TRUE, mutex_name.c_str());
        if (mutex_handle_ == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (mutex_handle_) {
                CloseHandle(mutex_handle_);
                mutex_handle_ = NULL;
            }
            return false;
        }
        return true;
    }

    void release_single_instance_lock() override {
        if (mutex_handle_) {
            ReleaseMutex(mutex_handle_);
            CloseHandle(mutex_handle_);
            mutex_handle_ = NULL;
        }
    }

    void setup_signal_handlers(std::function<void()> shutdown_cb) override {
        g_shutdown_cb = shutdown_cb;
        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    }

private:
    HANDLE mutex_handle_{NULL};
};

std::unique_ptr<i_platform> create_platform() {
    return std::make_unique<win32_platform>();
}

} // namespace apr
