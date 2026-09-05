#ifndef APR_PLATFORM_HPP
#define APR_PLATFORM_HPP

#include <string>
#include <functional>
#include <memory>

namespace apr {

class i_platform {
public:
    virtual ~i_platform() = default;

    virtual bool initialize_daemon(const std::string& service_name) = 0;
    virtual bool acquire_single_instance_lock(const std::string& app_name) = 0;
    virtual void release_single_instance_lock() = 0;
    virtual void setup_signal_handlers(std::function<void()> shutdown_cb) = 0;
};

std::unique_ptr<i_platform> create_platform();

} // namespace apr

#endif // APR_PLATFORM_HPP
