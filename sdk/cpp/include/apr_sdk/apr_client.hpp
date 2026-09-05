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

// Canonical APR connection procedure (mirrored - deliberately, not
// accidentally - across sdk/cpp, sdk/nodejs, sdk/ts, and sdk/csharp):
//   1. Open the MQTT connection (username "{role}_{suffix}", password
//      "{accessKey}{username}").
//   2. Subscribe to "apr/+" so topology events start queuing immediately.
//   3. Publish "apr/node/meta" with { role, workers, endpoint }.
//   4. Fetch a full registry snapshot over HTTP (GET /registry) to seed
//      local state without waiting for a slow trickle of individual events.
//   5. Drain the events that queued during steps 2-4 on top of the
//      snapshot, then switch to applying further events live.
// Any SDK client should be recognizable against this sequence; if you're
// implementing a 5th language binding, follow the same order.
class apr_client {
public:
    explicit apr_client(const client_options& opts);
    ~apr_client();

    // Returns false on failure (bad URL, connection refused, WS handshake
    // rejected, etc.) - call last_error() for a human-readable reason.
    bool start();
    void stop();

    // Reason the most recent start() call failed; empty if it succeeded or
    // hasn't been called yet.
    const std::string& last_error() const;

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
