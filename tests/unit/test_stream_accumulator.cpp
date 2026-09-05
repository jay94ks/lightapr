#include "apr/stream_accumulator.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

void test_stream_accumulator_append_and_view() {
    apr::stream_accumulator<std::vector<uint8_t>> acc;
    std::vector<uint8_t> chunk1 = {1, 2, 3};
    std::vector<uint8_t> chunk2 = {4, 5};

    acc.append(chunk1.data(), chunk1.size());
    acc.append(chunk2.data(), chunk2.size());

    assert(acc.size() == 5);
    assert(!acc.empty());
    assert(acc.data()[0] == 1);
    assert(acc.data()[4] == 5);

    std::cout << "[PASS] test_stream_accumulator_append_and_view" << std::endl;
}

void test_stream_accumulator_consume_advances_view() {
    apr::stream_accumulator<std::vector<uint8_t>> acc;
    std::vector<uint8_t> data = {10, 20, 30, 40, 50};
    acc.append(data.data(), data.size());

    acc.consume(2);
    assert(acc.size() == 3);
    assert(acc.data()[0] == 30);

    std::cout << "[PASS] test_stream_accumulator_consume_advances_view" << std::endl;
}

void test_stream_accumulator_compact_fully_consumed_clears() {
    apr::stream_accumulator<std::vector<uint8_t>> acc;
    std::vector<uint8_t> data = {1, 2, 3};
    acc.append(data.data(), data.size());
    acc.consume(3);
    assert(acc.empty());

    acc.compact_if_needed();
    assert(acc.total_capacity() == 0); // fully consumed -> backing storage reclaimed

    std::cout << "[PASS] test_stream_accumulator_compact_fully_consumed_clears" << std::endl;
}

void test_stream_accumulator_compact_preserves_unconsumed_tail() {
    apr::stream_accumulator<std::vector<uint8_t>> acc;
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    acc.append(data.data(), data.size());
    acc.consume(3); // 2 bytes remain unconsumed: {4, 5}

    acc.compact_if_needed(/*threshold=*/1); // force compaction even for a small prefix
    assert(acc.size() == 2);
    assert(acc.data()[0] == 4);
    assert(acc.data()[1] == 5);
    assert(acc.total_capacity() == 2); // the consumed prefix was actually reclaimed

    std::cout << "[PASS] test_stream_accumulator_compact_preserves_unconsumed_tail" << std::endl;
}

void test_stream_accumulator_below_threshold_does_not_compact() {
    apr::stream_accumulator<std::vector<uint8_t>> acc;
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    acc.append(data.data(), data.size());
    acc.consume(3);

    acc.compact_if_needed(/*threshold=*/4096); // consumed prefix (3) is below threshold
    assert(acc.size() == 2);
    // total_capacity still reflects the un-reclaimed consumed prefix + remaining tail.
    assert(acc.total_capacity() == 5);

    std::cout << "[PASS] test_stream_accumulator_below_threshold_does_not_compact" << std::endl;
}

void test_stream_accumulator_string_container() {
    apr::stream_accumulator<std::string> acc;
    std::string a = "hello ";
    std::string b = "world";
    acc.append(a.data(), a.size());
    acc.append(b.data(), b.size());

    std::string view(acc.data(), acc.size());
    assert(view == "hello world");

    acc.consume(6);
    std::string remaining(acc.data(), acc.size());
    assert(remaining == "world");

    std::cout << "[PASS] test_stream_accumulator_string_container" << std::endl;
}

void run_stream_accumulator_tests() {
    test_stream_accumulator_append_and_view();
    test_stream_accumulator_consume_advances_view();
    test_stream_accumulator_compact_fully_consumed_clears();
    test_stream_accumulator_compact_preserves_unconsumed_tail();
    test_stream_accumulator_below_threshold_does_not_compact();
    test_stream_accumulator_string_container();
}
