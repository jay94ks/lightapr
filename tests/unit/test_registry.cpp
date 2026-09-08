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

void test_registry_multiple_event_observers() {
    apr::registry reg;
    apr::endpoint_info ep{"127.0.0.1", 8080, "http"};

    int observer1_count = 0;
    int observer2_count = 0;

    auto token1 = reg.add_event_callback([&](const apr::node_info&) { observer1_count++; });
    auto token2 = reg.add_event_callback([&](const apr::node_info&) { observer2_count++; });

    reg.register_or_update_node("api", {}, ep, "127.0.0.1");
    assert(observer1_count == 1);
    assert(observer2_count == 1);

    // Removing one subscription must not affect the other.
    reg.remove_event_callback(token1);
    reg.register_or_update_node("api", {}, ep, "127.0.0.1");
    assert(observer1_count == 1); // unchanged
    assert(observer2_count == 2);

    reg.remove_event_callback(token2);
    reg.register_or_update_node("api", {}, ep, "127.0.0.1");
    assert(observer1_count == 1);
    assert(observer2_count == 2);

    std::cout << "[PASS] test_registry_multiple_event_observers" << std::endl;
}

void test_update_node_extra_valid_worker() {
    apr::registry reg;
    apr::endpoint_info ep{"127.0.0.1", 8080, "http"};

    auto node = reg.register_or_update_node("worker", {"task_a", "task_b"}, ep, "127.0.0.1");

    bool ok = reg.update_node_extra(node.id, "task_a", nlohmann::json{{"load", 0.5}});
    assert(ok);

    auto fetched = reg.get_node(node.id);
    assert(fetched.has_value());
    assert(fetched->extra.contains("task_a"));
    assert(fetched->extra["task_a"]["load"] == 0.5);
    // Untouched worker must not gain an entry.
    assert(!fetched->extra.contains("task_b"));

    std::cout << "[PASS] test_update_node_extra_valid_worker" << std::endl;
}

void test_update_node_extra_rejects_unknown_worker_and_node() {
    apr::registry reg;
    apr::endpoint_info ep{"127.0.0.1", 8080, "http"};

    auto node = reg.register_or_update_node("worker", {"task_a"}, ep, "127.0.0.1");

    // Not in this node's workers list.
    bool ok = reg.update_node_extra(node.id, "task_z", nlohmann::json{{"x", 1}});
    assert(!ok);
    auto fetched = reg.get_node(node.id);
    assert(fetched->extra.empty());

    // Unknown node id entirely.
    ok = reg.update_node_extra("no-such-node", "task_a", nlohmann::json{{"x", 1}});
    assert(!ok);

    std::cout << "[PASS] test_update_node_extra_rejects_unknown_worker_and_node" << std::endl;
}

void test_update_node_extra_rejects_oversized_payload_keeps_previous() {
    apr::registry reg(16); // 16-byte cap, deliberately tiny
    apr::endpoint_info ep{"127.0.0.1", 8080, "http"};

    auto node = reg.register_or_update_node("worker", {"task_a"}, ep, "127.0.0.1");

    bool ok = reg.update_node_extra(node.id, "task_a", nlohmann::json{{"v", 1}});
    assert(ok); // small enough

    auto before = reg.get_node(node.id)->extra["task_a"];

    ok = reg.update_node_extra(node.id, "task_a",
                                nlohmann::json{{"a_much_longer_key_that_blows_the_cap", "yes"}});
    assert(!ok);

    auto after = reg.get_node(node.id)->extra["task_a"];
    assert(before == after); // previous value untouched

    std::cout << "[PASS] test_update_node_extra_rejects_oversized_payload_keeps_previous" << std::endl;
}

void test_register_or_update_node_prunes_stale_extra_on_worker_removal() {
    apr::registry reg;
    apr::endpoint_info ep{"127.0.0.1", 8080, "http"};

    auto node = reg.register_or_update_node("worker", {"task_a", "task_b"}, ep, "127.0.0.1");
    reg.update_node_extra(node.id, "task_a", nlohmann::json{{"x", 1}});
    reg.update_node_extra(node.id, "task_b", nlohmann::json{{"y", 2}});

    // Re-register without task_b - its extra entry should be pruned, not left dangling.
    reg.register_or_update_node("worker", {"task_a"}, ep, "127.0.0.1", node.id);

    auto fetched = reg.get_node(node.id);
    assert(fetched->extra.contains("task_a"));
    assert(!fetched->extra.contains("task_b"));

    std::cout << "[PASS] test_register_or_update_node_prunes_stale_extra_on_worker_removal" << std::endl;
}

void test_register_or_update_node_accepts_initial_extra() {
    apr::registry reg;
    apr::endpoint_info ep{"127.0.0.1", 8080, "http"};

    nlohmann::json initial_extra = {{"task_a", {{"ready", true}}}, {"unknown_worker", {{"ignored", true}}}};
    auto node = reg.register_or_update_node("worker", {"task_a"}, ep, "127.0.0.1", "", initial_extra);

    assert(node.extra.contains("task_a"));
    assert(node.extra["task_a"]["ready"] == true);
    // Keys not in the declared workers list must be dropped, not silently accepted.
    assert(!node.extra.contains("unknown_worker"));

    std::cout << "[PASS] test_register_or_update_node_accepts_initial_extra" << std::endl;
}

void run_mqtt_packet_tests();
void run_websocket_tests();
void run_cli_options_tests();
void run_stream_accumulator_tests();
void run_registry_concurrency_tests();
void run_http_server_tests();
void run_connection_guard_tests();
void run_memory_tracker_tests();
void run_mqtt_broadcast_tests();

int main() {
    test_register_and_resolve();
    test_grace_period_and_sweep();
    test_query_pagination();
    test_null_endpoint();
    test_registry_multiple_event_observers();
    test_update_node_extra_valid_worker();
    test_update_node_extra_rejects_unknown_worker_and_node();
    test_update_node_extra_rejects_oversized_payload_keeps_previous();
    test_register_or_update_node_prunes_stale_extra_on_worker_removal();
    test_register_or_update_node_accepts_initial_extra();
    run_mqtt_packet_tests();
    run_websocket_tests();
    run_cli_options_tests();
    run_stream_accumulator_tests();
    run_registry_concurrency_tests();
    run_http_server_tests();
    run_connection_guard_tests();
    run_memory_tracker_tests();
    run_mqtt_broadcast_tests();
    std::cout << "All unit tests passed successfully!" << std::endl;
    return 0;
}
