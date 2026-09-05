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

    opts.role = "cpp-api-service";
    opts.workers = {"auth", "order"};
    opts.endpoint = apr::endpoint_info{"127.0.0.1", 3002, "http"};

    apr::sdk::apr_client client(opts);

    client.set_node_status_callback([](const apr::node_info& node) {
        std::cout << "[C++ HTTP Node] Node event: " << node.id << " (" << node.role << ") -> " << apr::to_string(node.status) << std::endl;
    });

    if (client.start()) {
        std::cout << "[C++ HTTP Node] Registered to LightAPR as role 'cpp-api-service' (endpoint: 3002, MQTT: " << opts.mqtt_url << ")" << std::endl;

        client.subscribe_app_event("cpp-api-service", "auth", [](const std::string& payload, const std::string& topic) {
            std::cout << "[C++ HTTP Node] Received app event on " << topic << ": " << payload << std::endl;
        });

        std::this_thread::sleep_for(std::chrono::seconds(10));
        client.stop();
    } else {
        std::cerr << "[C++ HTTP Node] Failed to connect to LightAPR" << std::endl;
    }

    return 0;
}
