#include "apr/memory_tracker.hpp"
#include <cassert>
#include <iostream>

// memory_tracker is a process-wide singleton, so these tests only assert
// relative deltas (before/after one call) rather than absolute values -
// other tests/background activity in the same process may also be
// contributing to the same counters.

void test_memory_tracker_registry_bytes_delta() {
    auto before = apr::memory_tracker::instance().get_stats().registry_kb;
    apr::memory_tracker::instance().add_registry_bytes(2048); // 2 KB
    auto after = apr::memory_tracker::instance().get_stats().registry_kb;
    assert(after == before + 2);

    apr::memory_tracker::instance().add_registry_bytes(-2048);
    auto restored = apr::memory_tracker::instance().get_stats().registry_kb;
    assert(restored == before);

    std::cout << "[PASS] test_memory_tracker_registry_bytes_delta" << std::endl;
}

void test_memory_tracker_mqtt_rx_tx_are_independent() {
    auto before_rx = apr::memory_tracker::instance().get_stats().mqtt_rx_kb;
    auto before_tx = apr::memory_tracker::instance().get_stats().mqtt_tx_kb;

    apr::memory_tracker::instance().add_mqtt_rx_bytes(4096);
    auto stats = apr::memory_tracker::instance().get_stats();
    assert(stats.mqtt_rx_kb == before_rx + 4);
    assert(stats.mqtt_tx_kb == before_tx); // tx untouched by an rx call

    apr::memory_tracker::instance().add_mqtt_tx_bytes(1024);
    stats = apr::memory_tracker::instance().get_stats();
    assert(stats.mqtt_tx_kb == before_tx + 1);

    std::cout << "[PASS] test_memory_tracker_mqtt_rx_tx_are_independent" << std::endl;
}

void test_memory_tracker_http_rx_tx_are_independent() {
    auto before_rx = apr::memory_tracker::instance().get_stats().http_rx_kb;
    auto before_tx = apr::memory_tracker::instance().get_stats().http_tx_kb;

    apr::memory_tracker::instance().add_http_rx_bytes(3072);
    apr::memory_tracker::instance().add_http_tx_bytes(5120);

    auto stats = apr::memory_tracker::instance().get_stats();
    assert(stats.http_rx_kb == before_rx + 3);
    assert(stats.http_tx_kb == before_tx + 5);

    std::cout << "[PASS] test_memory_tracker_http_rx_tx_are_independent" << std::endl;
}

void test_memory_tracker_negative_never_goes_below_zero() {
    // A category driven far negative (e.g. a bookkeeping edge case) must
    // clamp to 0 in get_stats() rather than reporting a nonsensical
    // negative KB count.
    apr::memory_tracker::instance().add_other_bytes(-1000000000);
    auto stats = apr::memory_tracker::instance().get_stats();
    assert(stats.other_kb == 0);

    // Restore it back towards zero so this test doesn't permanently poison
    // the process-wide singleton for any test that runs after it.
    apr::memory_tracker::instance().add_other_bytes(1000000000);

    std::cout << "[PASS] test_memory_tracker_negative_never_goes_below_zero" << std::endl;
}

void test_memory_tracker_total_reflects_process_rss() {
    auto stats = apr::memory_tracker::instance().get_stats();
    // A running process always has a nonzero RSS; this is mostly a smoke
    // test that get_process_rss_kb() returns something plausible on this
    // platform rather than silently failing to 0.
    assert(stats.total_kb > 0);

    std::cout << "[PASS] test_memory_tracker_total_reflects_process_rss" << std::endl;
}

void run_memory_tracker_tests() {
    test_memory_tracker_registry_bytes_delta();
    test_memory_tracker_mqtt_rx_tx_are_independent();
    test_memory_tracker_http_rx_tx_are_independent();
    test_memory_tracker_negative_never_goes_below_zero();
    test_memory_tracker_total_reflects_process_rss();
}
