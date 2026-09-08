// End-to-end tests that a real mqtt_server, with real sockets, actually
// delivers apr/{role} broadcasts to every apr/+ subscriber - regardless of
// whether that subscriber shares the role of the node whose state changed.
// Every other test in this suite exercises either the codec (test_mqtt_
// packet.cpp) or the registry in isolation (test_registry.cpp); neither
// proves the wiring between "registry event fires" and "bytes land on a
// socket belonging to an unrelated role" actually holds - which is exactly
// the class of bug that once left app/{role} publishes silently dropped.
#include "../../src/mqtt/mqtt_server.hpp"
#include "../../src/mqtt/mqtt_packet.hpp"
#include "apr/registry.hpp"
#include "apr/connection_guard.hpp"
#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace {

// Hand-rolled client-side CONNECT/SUBSCRIBE encoders - the production codec
// only ever needs to decode these (the server receives them), so nothing
// equivalent exists to reuse. Mirrors test_mqtt_packet.cpp's
// build_connect_packet, duplicated locally since that one has file-local
// linkage.
std::vector<uint8_t> build_connect_packet(const std::string& client_id) {
    std::vector<uint8_t> body;
    body.push_back(0x00); body.push_back(0x04);
    body.insert(body.end(), {'M', 'Q', 'T', 'T'});
    body.push_back(0x04); // protocol level
    body.push_back(0x02); // connect flags: clean session
    body.push_back(0x00); body.push_back(0x3C); // keep alive = 60

    body.push_back(static_cast<uint8_t>((client_id.size() >> 8) & 0xFF));
    body.push_back(static_cast<uint8_t>(client_id.size() & 0xFF));
    body.insert(body.end(), client_id.begin(), client_id.end());

    std::vector<uint8_t> packet;
    packet.push_back(0x10);
    packet.push_back(static_cast<uint8_t>(body.size())); // remaining length (<128)
    packet.insert(packet.end(), body.begin(), body.end());
    return packet;
}

std::vector<uint8_t> build_subscribe_packet(const std::string& topic, uint16_t packet_id = 1) {
    std::vector<uint8_t> body;
    body.push_back(static_cast<uint8_t>((packet_id >> 8) & 0xFF));
    body.push_back(static_cast<uint8_t>(packet_id & 0xFF));
    body.push_back(static_cast<uint8_t>((topic.size() >> 8) & 0xFF));
    body.push_back(static_cast<uint8_t>(topic.size() & 0xFF));
    body.insert(body.end(), topic.begin(), topic.end());
    body.push_back(0x00); // requested QoS 0

    std::vector<uint8_t> packet;
    packet.push_back(0x82);
    packet.push_back(static_cast<uint8_t>(body.size())); // remaining length (<128)
    packet.insert(packet.end(), body.begin(), body.end());
    return packet;
}

// Scans an accumulated receive buffer for a PUBLISH frame whose topic
// matches, at any byte offset - simpler than tracking exact frame
// boundaries across CONNACK/SUBACK/PUBLISH, and false positives are a
// non-issue at test scale since the topic string itself is checked.
bool find_publish_with_topic(const std::vector<uint8_t>& buf, const std::string& want_topic, std::string& out_payload) {
    for (size_t i = 0; i < buf.size(); ++i) {
        if ((buf[i] & 0xF0) != 0x30) continue;
        apr::mqtt_publish pub;
        if (apr::mqtt_codec::decode_publish(buf.data() + i, buf.size() - i, pub) && pub.topic == want_topic) {
            out_payload = pub.payload;
            return true;
        }
    }
    return false;
}

// A minimal raw MQTT client with its own io_context, so its blocking
// connect/read calls never touch the server's io_context thread.
struct raw_client {
    asio::io_context io_ctx;
    asio::ip::tcp::socket sock{io_ctx};
    std::vector<uint8_t> rx_buf;

    void connect(uint16_t port) {
        sock.connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
    }

    void send(const std::vector<uint8_t>& bytes) {
        asio::write(sock, asio::buffer(bytes));
    }

    void pump_rx(int ms) {
        sock.non_blocking(true);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        uint8_t tmp[4096];
        while (std::chrono::steady_clock::now() < deadline) {
            std::error_code ec;
            size_t n = sock.read_some(asio::buffer(tmp), ec);
            if (!ec && n > 0) {
                rx_buf.insert(rx_buf.end(), tmp, tmp + n);
            } else if (ec && ec != asio::error::would_block) {
                break;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    bool wait_for_publish(const std::string& want_topic, std::string& out_payload, int total_timeout_ms) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(total_timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            pump_rx(50);
            if (find_publish_with_topic(rx_buf, want_topic, out_payload)) return true;
        }
        return false;
    }

    void close() {
        std::error_code ec;
        sock.close(ec);
    }
};

} // namespace

void test_grace_and_extra_broadcast_reach_all_apr_plus_subscribers() {
    apr::registry reg;
    apr::connection_guard conn_guard(apr::connection_limits{}); // all-zero = unlimited

    asio::io_context io_ctx;
    apr::mqtt_server server(io_ctx, /*port=*/0, reg, /*access_key=*/"", conn_guard);
    server.start();
    uint16_t port = server.local_port();

    auto work_guard = asio::make_work_guard(io_ctx);
    std::thread server_thread([&io_ctx] { io_ctx.run(); });

    raw_client node_a, observer_b;
    node_a.connect(port);
    observer_b.connect(port);

    node_a.send(build_connect_packet("node-a"));
    observer_b.send(build_connect_packet("observer-b"));

    // Both subscribe to apr/+ - the wildcard PROTOCOL.md's boot procedure
    // mandates every node use - not to "apr/roleA" specifically. observer_b
    // never registers itself as any role at all, so it receiving roleA's
    // broadcasts below is only possible via that wildcard, not shared role.
    node_a.send(build_subscribe_packet("apr/+"));
    observer_b.send(build_subscribe_packet("apr/+"));

    nlohmann::json meta_a = {
        {"role", "roleA"},
        {"workers", {"task_a"}},
        {"endpoint", nullptr}
    };
    node_a.send(apr::mqtt_codec::encode_publish("apr/node/meta", meta_a.dump()));

    std::string registered_payload;
    bool saw_registration = observer_b.wait_for_publish("apr/roleA", registered_payload, 5000);
    assert(saw_registration);
    auto reg_json = nlohmann::json::parse(registered_payload);
    assert(reg_json["status"] == "OK");
    std::string node_id = reg_json["id"].get<std::string>();

    // --- apr/node/extra: cross-role delivery check ---
    observer_b.rx_buf.clear();
    nlohmann::json extra_update = {{"worker", "task_a"}, {"extra", {{"load", 0.75}}}};
    node_a.send(apr::mqtt_codec::encode_publish("apr/node/extra", extra_update.dump()));

    std::string extra_payload;
    bool saw_extra = observer_b.wait_for_publish("apr/roleA", extra_payload, 5000);
    assert(saw_extra);
    auto extra_json = nlohmann::json::parse(extra_payload);
    assert(extra_json["extra"]["task_a"]["load"] == 0.75);

    // --- GRACE-on-disconnect: cross-role delivery check ---
    observer_b.rx_buf.clear();
    node_a.close(); // triggers on_before_close -> registry.mark_node_grace()

    std::string grace_payload;
    bool saw_grace = observer_b.wait_for_publish("apr/roleA", grace_payload, 5000);
    assert(saw_grace);
    auto grace_json = nlohmann::json::parse(grace_payload);
    assert(grace_json["status"] == "GRACE");
    assert(grace_json["id"] == node_id);
    // GRACE must not have wiped the worker's extra announcement.
    assert(grace_json["extra"]["task_a"]["load"] == 0.75);

    observer_b.close();
    io_ctx.stop();
    server_thread.join();

    std::cout << "[PASS] test_grace_and_extra_broadcast_reach_all_apr_plus_subscribers" << std::endl;
}

void run_mqtt_broadcast_tests() {
    test_grace_and_extra_broadcast_reach_all_apr_plus_subscribers();
}
