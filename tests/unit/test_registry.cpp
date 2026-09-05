#include "apr/registry.hpp"
#include <cassert>
#include <iostream>
#include <vector>

void test_register_and_resolve() {
    apr::registry reg;
    apr::endpoint_info ep{"127.0.0.1", 8080, "http"};

    auto node1 = reg.register_or_update_node("api", {"auth", "payment"}, ep, "127.0.0.1");
    assert(!node1.id.empty());
    assert(node1.role == "api");
    assert(node1.status == apr::node_status::ok);

    auto node2 = reg.register_or_update_node("api", {"auth"}, ep, "127.0.0.1");
    assert(node2.id != node1.id);

    // Resolve round-robin
    auto res1 = reg.resolve_node("api", "auth");
    assert(res1.has_value());
    
    auto res2 = reg.resolve_node("api", "auth");
    assert(res2.has_value());

    // Round robin should cycle
    assert(res1->id != res2->id || res1->id == node1.id);

    std::cout << "[PASS] test_register_and_resolve" << std::endl;
}

void test_grace_period_and_sweep() {
    apr::registry reg;
    apr::endpoint_info ep{"127.0.0.1", 8080, "http"};

    auto node = reg.register_or_update_node("worker", {"task"}, ep, "127.0.0.1");
    assert(node.status == apr::node_status::ok);

    reg.mark_node_grace(node.id);
    auto fetched = reg.get_node(node.id);
    assert(fetched.has_value());
    assert(fetched->status == apr::node_status::grace);
    assert(fetched->expires_in.has_value());

    // Resolving OK nodes should not pick grace nodes
    auto resolved = reg.resolve_node("worker", "task");
    assert(!resolved.has_value());

    std::cout << "[PASS] test_grace_period_and_sweep" << std::endl;
}

void test_query_pagination() {
    apr::registry reg;
    apr::endpoint_info ep{"127.0.0.1", 8080, "http"};

    for (int i = 0; i < 5; ++i) {
        reg.register_or_update_node("web", {"frontend"}, ep, "127.0.0.1");
    }

    auto [total, nodes] = reg.query_registry(1, 2, "web");
    assert(total == 5);
    assert(nodes.size() == 2);

    auto [total2, nodes2] = reg.query_registry(3, 2, "web");
    assert(total2 == 5);
    assert(nodes2.size() == 1);

    std::cout << "[PASS] test_query_pagination" << std::endl;
}

void test_null_endpoint() {
    apr::registry reg;

    // Register node with std::nullopt (null endpoint)
    auto node = reg.register_or_update_node("worker-only", {"job"}, std::nullopt, "127.0.0.1");
    assert(!node.id.empty());
    assert(node.role == "worker-only");
    assert(!node.endpoint.has_value());

    // Query registry and verify endpoint is null in JSON
    auto [total, nodes] = reg.query_registry(1, 10, "worker-only");
    assert(total == 1);
    assert(!nodes[0].endpoint.has_value());

    nlohmann::json j = nodes[0];
    assert(j["endpoint"].is_null());

    // Resolve node and verify endpoint is null
    auto resolved = reg.resolve_node("worker-only", "job");
    assert(resolved.has_value());
    assert(!resolved->endpoint.has_value());

    std::cout << "[PASS] test_null_endpoint" << std::endl;
}

void run_mqtt_packet_tests();
void run_websocket_tests();

int main() {
    test_register_and_resolve();
    test_grace_period_and_sweep();
    test_query_pagination();
    test_null_endpoint();
    run_mqtt_packet_tests();
    run_websocket_tests();
    std::cout << "All unit tests passed successfully!" << std::endl;
    return 0;
}
