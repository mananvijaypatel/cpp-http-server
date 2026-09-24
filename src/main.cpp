#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "http/request.hpp"
#include "http/response.hpp"
#include "http/router.hpp"
#include "net/event_loop.hpp"
#include "net/socket.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kIdleTimeout = std::chrono::seconds(15);
constexpr int kSweepIntervalMs = 1000;

// Everything one connection needs. With a single thread multiplexing many
// connections, this state can no longer live in local variables.
struct Connection {
    net::Socket socket;
    std::string in;                    // received, not yet parsed
    std::string out;                   // produced, not yet sent
    bool close_after_flush = false;    // finish writing, then hang up
    std::uint32_t interest = EPOLLIN;  // what we last told epoll to watch
    Clock::time_point deadline;

    explicit Connection(net::Socket s)
        : socket(std::move(s)), deadline(Clock::now() + kIdleTimeout) {}
};

using ConnectionMap = std::unordered_map<int, Connection>;

std::string errno_message(int err) {
    return std::generic_category().message(err);
}

// Drain the socket into conn.in. Returns false if the connection is finished.
bool read_available(Connection& conn) {
    char chunk[4096];

    while (true) {
        ssize_t n = ::recv(conn.socket.fd(), chunk, sizeof(chunk), 0);

        if (n > 0) {
            conn.in.append(chunk, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            return false;  // peer closed cleanly
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;  // socket drained: this is the normal exit
        }
        std::cerr << "recv: " << errno_message(errno) << '\n';
        return false;
    }
}

// Parse every complete request in the buffer and queue the responses.
// Returns false if the connection should close once conn.out is flushed.
bool process_requests(Connection& conn) {
    while (true) {
        http::ParseResult parsed = http::parse_request_head(conn.in);

        if (parsed.status == http::ParseStatus::Error) {
            auto resp = http::make_response(parsed.error_status, "text/plain; charset=utf-8",
                                            parsed.error + "\n");
            conn.out += http::serialize(resp, false, false);
            std::cout << "rejected request: " << parsed.error << '\n';
            return false;
        }

        if (parsed.status == http::ParseStatus::Incomplete) {
            return true;  // need more bytes; the parser caps the buffer at 8 KB
        }

        const http::Request& req = parsed.request;
        conn.in.erase(0, parsed.consumed);

        bool keep_alive = http::wants_keep_alive(req) && !http::has_body(req);

        auto start = Clock::now();
        http::Response resp = http::route(req);
        conn.out += http::serialize(resp, keep_alive, req.method == "HEAD");
        auto us =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();

        std::cout << req.method << ' ' << req.target << " -> " << resp.status << " (" << us
                  << "us)\n";

        if (!keep_alive) {
            return false;
        }
    }
}

// Send as much of conn.out as the kernel will take. Returns false on a fatal error.
bool flush_output(Connection& conn) {
    while (!conn.out.empty()) {
        ssize_t n = ::send(conn.socket.fd(), conn.out.data(), conn.out.size(), MSG_NOSIGNAL);

        if (n > 0) {
            conn.out.erase(0, static_cast<std::size_t>(n));
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;  // kernel send buffer full: retry on EPOLLOUT
        }
        return false;  // client vanished
    }
    return true;
}

// Tell epoll what we care about now, but only if it changed.
void update_interest(net::EventLoop& loop, Connection& conn) {
    std::uint32_t events = conn.close_after_flush ? 0U : static_cast<std::uint32_t>(EPOLLIN);
    if (!conn.out.empty()) {
        events |= EPOLLOUT;
    }
    if (events != conn.interest) {
        loop.modify(conn.socket.fd(), events);
        conn.interest = events;
    }
}

// Handle one ready connection. Returns false when it should be dropped.
bool service(Connection& conn, std::uint32_t events) {
    if ((events & (EPOLLERR | EPOLLHUP)) != 0) {
        return false;
    }

    if ((events & EPOLLIN) != 0) {
        if (!read_available(conn)) {
            return false;
        }
        conn.deadline = Clock::now() + kIdleTimeout;
        if (!process_requests(conn)) {
            conn.close_after_flush = true;
        }
    }

    // Always try to write: a response queued just now usually fits immediately,
    // which saves a whole epoll round trip.
    if (!flush_output(conn)) {
        return false;
    }

    return !(conn.close_after_flush && conn.out.empty());
}

// Accept every pending connection until the backlog is empty.
void accept_all(const net::Socket& listener, net::EventLoop& loop, ConnectionMap& conns) {
    while (true) {
        int fd = ::accept4(listener.fd(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);

        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;  // no more waiting connections
            }
            if (errno == EMFILE || errno == ENFILE) {
                // Out of file descriptors. The listener stays readable, so
                // returning here means we retry on the next loop iteration
                // instead of spinning on a failing accept4().
                std::cerr << "accept: out of file descriptors, deferring\n";
                return;
            }
            std::cerr << "accept: " << errno_message(errno) << '\n';
            return;
        }

        net::Socket client{fd};
        loop.add(fd, EPOLLIN);
        conns.try_emplace(fd, std::move(client));
    }
}

// Close connections that have gone quiet for too long.
void sweep_timeouts(net::EventLoop& loop, ConnectionMap& conns) {
    auto now = Clock::now();
    for (auto it = conns.begin(); it != conns.end();) {
        if (it->second.deadline <= now) {
            loop.remove(it->first);
            it = conns.erase(it);  // erase returns the next valid iterator
        } else {
            ++it;
        }
    }
}

}  // namespace

int main() {
    try {
        net::EventLoop loop;
        auto listener = net::listen_tcp(8080);
        net::set_nonblocking(listener);
        loop.add(listener.fd(), EPOLLIN);

        ConnectionMap conns;

        std::cout << "HTTP server (epoll, single thread) listening on http://127.0.0.1:8080\n";
        std::cout << "sizeof(Connection) = " << sizeof(Connection) << " bytes\n";
        while (true) {
            for (const epoll_event& ev : loop.wait(kSweepIntervalMs)) {
                if (ev.data.fd == listener.fd()) {
                    accept_all(listener, loop, conns);
                    continue;
                }

                auto it = conns.find(ev.data.fd);
                if (it == conns.end()) {
                    continue;  // already cleaned up this round
                }

                if (service(it->second, ev.events)) {
                    update_interest(loop, it->second);
                } else {
                    loop.remove(it->first);
                    conns.erase(it);
                }
            }

            sweep_timeouts(loop, conns);
        }

    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }
}