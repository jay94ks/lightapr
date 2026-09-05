#include "../../src/http/http_parsing.hpp"
#include <cassert>
#include <iostream>

void test_http_parse_query_basic() {
    auto q = apr::parse_query("page=2&count=10&role=worker");
    assert(q.at("page") == "2");
    assert(q.at("count") == "10");
    assert(q.at("role") == "worker");
    std::cout << "[PASS] test_http_parse_query_basic" << std::endl;
}

void test_http_parse_query_edge_cases() {
    auto empty = apr::parse_query("");
    assert(empty.empty());

    auto single = apr::parse_query("role=worker");
    assert(single.size() == 1 && single.at("role") == "worker");

    auto no_value = apr::parse_query("flag");
    assert(no_value.at("flag") == "");

    auto trailing_amp = apr::parse_query("a=1&");
    assert(trailing_amp.at("a") == "1");

    std::cout << "[PASS] test_http_parse_query_edge_cases" << std::endl;
}

void test_http_parse_raw_http_basic_get() {
    std::string raw = "GET /registry?role=worker HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n";
    auto req = apr::parse_raw_http(raw);

    assert(req.method == "GET");
    assert(req.path == "/registry");
    assert(req.query_params.at("role") == "worker");
    assert(req.headers.at("Host") == "localhost");
    assert(req.keep_alive == true);
    std::cout << "[PASS] test_http_parse_raw_http_basic_get" << std::endl;
}

void test_http_parse_raw_http_keep_alive_defaults() {
    // HTTP/1.1 with no Connection header defaults to keep-alive.
    auto req11 = apr::parse_raw_http("GET /healthz HTTP/1.1\r\n\r\n");
    assert(req11.keep_alive == true);

    // HTTP/1.0 with no Connection header defaults to close.
    auto req10 = apr::parse_raw_http("GET /healthz HTTP/1.0\r\n\r\n");
    assert(req10.keep_alive == false);

    // Explicit Connection: close overrides the HTTP/1.1 default.
    auto reqClose = apr::parse_raw_http("GET /healthz HTTP/1.1\r\nConnection: close\r\n\r\n");
    assert(reqClose.keep_alive == false);

    // Explicit Connection: keep-alive overrides the HTTP/1.0 default.
    auto reqKeep = apr::parse_raw_http("GET /healthz HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");
    assert(reqKeep.keep_alive == true);

    std::cout << "[PASS] test_http_parse_raw_http_keep_alive_defaults" << std::endl;
}

void test_http_find_request_boundary_no_body() {
    std::string_view buffered = "GET /healthz HTTP/1.1\r\nHost: x\r\n\r\n";
    auto boundary = apr::find_request_boundary(buffered);
    assert(boundary.has_value());
    assert(*boundary == buffered.size());
    std::cout << "[PASS] test_http_find_request_boundary_no_body" << std::endl;
}

void test_http_find_request_boundary_incomplete_headers() {
    std::string_view buffered = "GET /healthz HTTP/1.1\r\nHost: x\r\n"; // no terminating blank line yet
    auto boundary = apr::find_request_boundary(buffered);
    assert(!boundary.has_value());
    std::cout << "[PASS] test_http_find_request_boundary_incomplete_headers" << std::endl;
}

void test_http_find_request_boundary_waits_for_body() {
    std::string headers = "POST /x HTTP/1.1\r\nContent-Length: 5\r\n\r\n";
    std::string partial_body = "ab"; // only 2 of 5 body bytes buffered so far
    std::string buffered = headers + partial_body;

    auto boundary = apr::find_request_boundary(buffered);
    assert(!boundary.has_value());

    std::string full = headers + "abcde";
    auto boundary2 = apr::find_request_boundary(full);
    assert(boundary2.has_value());
    assert(*boundary2 == full.size());

    std::cout << "[PASS] test_http_find_request_boundary_waits_for_body" << std::endl;
}

void test_http_find_request_boundary_pipelined_requests() {
    std::string req1 = "GET /a HTTP/1.1\r\n\r\n";
    std::string req2 = "GET /b HTTP/1.1\r\n\r\n";
    std::string buffered = req1 + req2;

    auto boundary = apr::find_request_boundary(buffered);
    assert(boundary.has_value());
    assert(*boundary == req1.size()); // only the first request's length, not both

    std::cout << "[PASS] test_http_find_request_boundary_pipelined_requests" << std::endl;
}

void run_http_server_tests() {
    test_http_parse_query_basic();
    test_http_parse_query_edge_cases();
    test_http_parse_raw_http_basic_get();
    test_http_parse_raw_http_keep_alive_defaults();
    test_http_find_request_boundary_no_body();
    test_http_find_request_boundary_incomplete_headers();
    test_http_find_request_boundary_waits_for_body();
    test_http_find_request_boundary_pipelined_requests();
}
