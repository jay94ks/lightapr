#ifndef APR_SDK_APR_CLIENT_HPP
#define APR_SDK_APR_CLIENT_HPP

#include "apr/node.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

namespace apr::sdk {

using app_event_callback = std::function<void(const std::string& payload, const std::string& topic)>;
using node_status_callback = std::function<void(const apr::node_info& node)>;

struct client_options {
    std::string mqtt_url{"mqtt://127.0.0.1:1883"};
    std::string http_url{"http://127.0.0.1:8080"};
    std::string role{"default-role"};
    std::vector<std::string> workers;
    std::optional<apr::endpoint_info> endpoint;
    std::string access_key{"lightapr_secret_key"};
};

class apr_client {
public:
    explicit apr_client(const client_options& opts);
    ~apr_client();

    bool start();
    void stop();

    std::optional<apr::node_info> resolve_node(const std::string& role, const std::string& worker = "");
    std::vector<apr::node_info> get_local_registry() const;

    void publish_app_event(const std::string& target_role, const std::string& worker, const std::string& payload);
    void subscribe_app_event(const std::string& target_role, const std::string& worker, app_event_callback cb);

    void set_node_status_callback(node_status_callback cb);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace apr::sdk

#endif // APR_SDK_APR_CLIENT_HPP
