#ifndef APR_TCP_SESSION_HPP
#define APR_TCP_SESSION_HPP

#include "apr/logger.hpp"
#include <asio.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace apr {

inline constexpr int64_t k_default_session_idle_timeout_sec = 90;

// CRTP base for a per-connection asio TCP session. Owns the socket, a strand
// serializing this session's async completions, a fixed-size read buffer,
// and an idle-timeout timer - the pieces that used to be duplicated
// identically between mqtt_session and http_session.
//
// Derived (e.g. mqtt_session, http_session) must:
//  - publicly inherit tcp_session_base<Derived, RxBufSize>
//  - declare `friend class tcp_session_base<Derived, RxBufSize>;` so the base
//    can invoke the private hooks below via static_cast<Derived*>(this)
//  - implement:
//      void on_bytes_read(const uint8_t* data, size_t n);
//          Process newly-read bytes. Call start_read_loop() again to keep
//          reading, or return without doing so to stop (e.g. after closing).
//      void on_before_close();
//          Protocol cleanup safe to run from a destructor - MUST NOT call
//          shared_from_this() (the session's refcount may already be zero).
//      void on_session_closed();
//          Called by the base right after a base-initiated close (read
//          error or idle timeout). Always runs from a live async handler
//          that already holds a shared_ptr to this session on its stack, so
//          it IS safe to call shared_from_this() here (e.g. to notify an
//          owning container). Derived classes may also call this themselves
//          after their own close_session() calls, for the same "notify my
//          owner" step, to avoid duplicating that logic at every close site.
//
// Safety rule: close_session() never calls shared_from_this() - directly, or
// transitively via on_before_close() - since it also runs from ~Derived().
template <typename Derived, size_t RxBufSize = 4096>
class tcp_session_base : public std::enable_shared_from_this<Derived> {
protected:
    tcp_session_base(asio::ip::tcp::socket socket,
                      std::chrono::seconds idle_timeout,
                      size_t max_buffer_bytes)
        : socket_(std::move(socket)),
          strand_(asio::make_strand(socket_.get_executor())),
          idle_timer_(strand_),
          idle_timeout_(idle_timeout),
          max_buffer_bytes_(max_buffer_bytes) {
        std::error_code ec;
        auto remote = socket_.remote_endpoint(ec);
        if (!ec) {
            peer_ip_ = remote.address().to_string();
        }
    }

    // Issues the next async read, (re)arming the idle timer. On completion,
    // cancels the timer; a read error closes the session via the
    // base-initiated close path, otherwise Derived::on_bytes_read is invoked.
    void start_read_loop() {
        arm_idle_timer();
        auto self = this->shared_from_this();
        socket_.async_read_some(asio::buffer(rx_buffer_, sizeof(rx_buffer_)),
            asio::bind_executor(strand_, [self, this](std::error_code ec, size_t bytes_transferred) {
                cancel_idle_timer();
                if (ec) {
                    close_session();
                    static_cast<Derived*>(this)->on_session_closed();
                    return;
                }
                static_cast<Derived*>(this)->on_bytes_read(rx_buffer_, bytes_transferred);
            }));
    }

    // Idempotent. Cancels the idle timer, closes the socket, and invokes
    // Derived::on_before_close(). Never touches shared_from_this() - safe to
    // call from a destructor.
    void close_session() {
        if (closed_) return;
        closed_ = true;
        cancel_idle_timer();
        std::error_code ec;
        socket_.close(ec);
        static_cast<Derived*>(this)->on_before_close();
    }

    bool exceeds_buffer_cap(size_t current_size) const {
        return current_size > max_buffer_bytes_;
    }

    asio::ip::tcp::socket socket_;
    asio::strand<asio::any_io_executor> strand_;
    std::string peer_ip_;
    bool closed_{false};

private:
    void arm_idle_timer() {
        idle_timer_.expires_after(idle_timeout_);
        auto self = this->shared_from_this();
        idle_timer_.async_wait(asio::bind_executor(strand_, [self, this](std::error_code ec) {
            if (ec) return; // canceled by a completed read, or the timer was destroyed
            LOG_WARN("Session idle timeout exceeded for peer: " + peer_ip_);
            close_session();
            static_cast<Derived*>(this)->on_session_closed();
        }));
    }

    void cancel_idle_timer() {
        std::error_code ec;
        idle_timer_.cancel(ec);
    }

    uint8_t rx_buffer_[RxBufSize];
    asio::steady_timer idle_timer_;
    std::chrono::seconds idle_timeout_;
    size_t max_buffer_bytes_;
};

} // namespace apr

#endif // APR_TCP_SESSION_HPP
