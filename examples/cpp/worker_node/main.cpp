#include "apr_sdk/apr_client.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>

int main() {
    apr::sdk::client_options opts;
    if (const char* env_mqtt = std::getenv("APR_MQTT_URL")) opts.mqtt_url = env_mqtt;
    else opts.mqtt_url = "mqtt://127.0.0.1:1883";

    if (const char* env_http = std::getenv("APR_HTTP_URL")) opts.http_url = env_http;
    else opts.http_url = "http://127.0.0.1:8080";

    if (const char* env_key = std::getenv("APR_ACCESS_KEY")) opts.access_key = env_key;
    else opts.access_key = "lightapr_secret_key";

    opts.role = "cpp-worker";
    opts.workers = {"compute-task"};
    opts.endpoint = std::nullopt; // No HTTP endpoint

    apr::sdk::apr_client client(opts);

    client.set_node_status_callback([](const apr::node_info& node) {
        std::cout << "[C++ Worker Node] Topology event: " << node.id << " (" << node.role << ") -> " << apr::to_string(node.status) << std::endl;
    });

    if (client.start()) {
        std::cout << "[C++ Worker Node] Registered to LightAPR as role 'cpp-worker' (endpoint: null, MQTT: " << opts.mqtt_url << ")" << std::endl;

        for (int i = 0; i < 3; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            auto target = client.resolve_node("cpp-api-service", "auth");
            if (target.has_value()) {
                std::cout << "[C++ Worker Node] Resolved 'cpp-api-service' -> ID: " << target->id << std::endl;
                client.publish_app_event("cpp-api-service", "auth", "{\"task\":\"validate\",\"id\":123}");
            } else {
                std::cout << "[C++ Worker Node] 'cpp-api-service' not found in local registry" << std::endl;
            }
        }

        client.stop();
    } else {
        std::cerr << "[C++ Worker Node] Failed to start client" << std::endl;
    }

    return 0;
}
