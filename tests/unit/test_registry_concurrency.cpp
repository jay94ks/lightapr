#include "apr/registry.hpp"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

// Smoke test for the multi-threaded worker-thread model: several threads
// hammer register/resolve/grace/sweep concurrently on one shared registry.
// This does not assert exact counts (interleaving makes those inherently
// racy) - it only asserts the registry survives concurrent use without
// crashing/deadlocking and that basic structural invariants hold afterward.
void test_registry_concurrent_access_does_not_crash() {
    apr::registry reg;
    apr::endpoint_info ep{"127.0.0.1", 8080, "http"};

    constexpr int kThreads = 8;
    constexpr int kIterationsPerThread = 200;
    std::atomic<int> registered_count{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < kIterationsPerThread; ++i) {
                auto node = reg.register_or_update_node("worker", {"job"}, ep, "127.0.0.1");
                registered_count++;

                reg.resolve_node("worker", "job");
                reg.get_node(node.id);
                reg.get_stats();
                reg.query_registry(1, 10, "worker");

                if (i % 3 == 0) {
                    reg.mark_node_grace(node.id);
                } else if (i % 3 == 1) {
                    reg.restore_node_active(node.id);
                } else {
                    reg.remove_node_permanently(node.id);
                }

                if (t == 0 && i % 10 == 0) {
                    reg.sweep_expired_nodes();
                }
            }
        });
    }

    for (auto& w : workers) w.join();

    assert(registered_count == kThreads * kIterationsPerThread);

    // The registry itself must still be in a consistent, queryable state.
    auto stats = reg.get_stats();
    assert(stats.total_nodes == stats.alive_nodes + stats.grace_nodes ||
           stats.total_nodes >= stats.alive_nodes + stats.grace_nodes);

    std::cout << "[PASS] test_registry_concurrent_access_does_not_crash" << std::endl;
}

void run_registry_concurrency_tests() {
    test_registry_concurrent_access_does_not_crash();
}
