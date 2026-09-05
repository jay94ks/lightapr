#include "apr/connection_guard.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

void test_connection_guard_global_limit() {
    apr::connection_limits limits;
    limits.max_total_connections = 2;
    apr::connection_guard guard(limits);

    assert(guard.try_acquire("1.1.1.1"));
    assert(guard.try_acquire("2.2.2.2"));
    assert(!guard.try_acquire("3.3.3.3")); // global cap reached
    assert(guard.total_connections() == 2);

    guard.release("1.1.1.1");
    assert(guard.total_connections() == 1);
    assert(guard.try_acquire("3.3.3.3")); // a slot freed up

    std::cout << "[PASS] test_connection_guard_global_limit" << std::endl;
}

void test_connection_guard_per_ip_limit() {
    apr::connection_limits limits;
    limits.max_connections_per_ip = 2;
    apr::connection_guard guard(limits);

    assert(guard.try_acquire("1.1.1.1"));
    assert(guard.try_acquire("1.1.1.1"));
    assert(!guard.try_acquire("1.1.1.1")); // per-IP cap reached

    // A different IP is unaffected by the first IP's cap.
    assert(guard.try_acquire("2.2.2.2"));

    guard.release("1.1.1.1");
    assert(guard.try_acquire("1.1.1.1")); // freed one slot for that IP

    std::cout << "[PASS] test_connection_guard_per_ip_limit" << std::endl;
}

void test_connection_guard_rate_limit() {
    apr::connection_limits limits;
    limits.max_new_connections_per_ip = 3;
    limits.rate_window = std::chrono::seconds(60); // long window - won't expire mid-test
    apr::connection_guard guard(limits);

    assert(guard.try_acquire("1.1.1.1"));
    assert(guard.try_acquire("1.1.1.1"));
    assert(guard.try_acquire("1.1.1.1"));
    assert(!guard.try_acquire("1.1.1.1")); // 4th new connection within the window is rejected

    // A different IP has its own independent rate budget.
    assert(guard.try_acquire("2.2.2.2"));

    std::cout << "[PASS] test_connection_guard_rate_limit" << std::endl;
}

void test_connection_guard_unlimited_by_default() {
    apr::connection_guard guard(apr::connection_limits{}); // all zero = unlimited
    for (int i = 0; i < 50; ++i) {
        assert(guard.try_acquire("1.1.1.1"));
    }
    assert(guard.total_connections() == 50);

    std::cout << "[PASS] test_connection_guard_unlimited_by_default" << std::endl;
}

void test_connection_guard_concurrent_use() {
    apr::connection_limits limits;
    limits.max_total_connections = 1000000; // effectively unlimited for this test
    apr::connection_guard guard(limits);

    constexpr int kThreads = 8;
    constexpr int kIterations = 500;
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            std::string ip = "10.0.0." + std::to_string(t);
            for (int i = 0; i < kIterations; ++i) {
                if (guard.try_acquire(ip)) {
                    guard.release(ip);
                }
            }
        });
    }
    for (auto& w : workers) w.join();

    assert(guard.total_connections() == 0); // every acquire was released

    std::cout << "[PASS] test_connection_guard_concurrent_use" << std::endl;
}

void run_connection_guard_tests() {
    test_connection_guard_global_limit();
    test_connection_guard_per_ip_limit();
    test_connection_guard_rate_limit();
    test_connection_guard_unlimited_by_default();
    test_connection_guard_concurrent_use();
}
