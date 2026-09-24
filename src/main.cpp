#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

#include <sys/socket.h>
#include <sys/types.h>

#include "http/request.hpp"
#include "http/response.hpp"
#include "http/router.hpp"
#include "net/socket.hpp"
#include "util/thread_pool.hpp"

namespace {

std::atomic<int> g_active_clients{0};

// Close keep-alive connections that stay silent this long.
constexpr auto kIdleTimeout = std::chrono::seconds(5);

std::string errno_message(int err) {
    return std::generic_category().message(err);
}

// Read, parse, respond, and repeat (keep-alive) until the connection should close.
void serve_http(const net::Socket& client) {
    std::string buffer;  // bytes received but not yet parsed
    char chunk[4096];

    while (true) {
        // 1. Parse what we already have FIRST. The buffer may already contain
        //    a complete request (e.g. pipelined requests from one recv).
        http::ParseResult parsed = http::parse_request_head(buffer);

        if (parsed.status == http::ParseStatus::Error) {
            auto resp = http::make_response(parsed.error_status, "text/plain; charset=utf-8",
                                            parsed.error + "\n");
            net::send_all(client, http::serialize(resp, /*keep_alive=*/false,
                                                  /*head_request=*/false));
            std::cout << "rejected request: " << parsed.error << '\n';
            return;  // after a parse error the stream can't be trusted: close
        }

        if (parsed.status == http::ParseStatus::Complete) {
            const http::Request& req = parsed.request;
            buffer.erase(0, parsed.consumed);  // drop this request's bytes

            // We don't read bodies yet, so a body would be misread as the
            // next request. If there is one, answer and then close.
            bool keep_alive = http::wants_keep_alive(req) && !http::has_body(req);

            auto start = std::chrono::steady_clock::now();

            http::Response resp = http::route(req);
            net::send_all(client, http::serialize(resp, keep_alive, req.method == "HEAD"));

            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();

            std::cout << req.method << ' ' << req.target << " -> " << resp.status << " (" << us
                      << "us)\n";

            if (!keep_alive) {
                return;
            }
            continue;  // another request might already be buffered
        }

        // 2. Incomplete: read more bytes from the client.
        ssize_t n = ::recv(client.fd(), chunk, sizeof(chunk), 0);

        if (n > 0) {
            buffer.append(chunk, static_cast<std::size_t>(n));
        } else if (n == 0) {
            return;  // client closed the connection
        } else {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;  // idle timeout: quietly close
            }
            std::cerr << "recv: " << errno_message(errno) << '\n';
            return;
        }
    }
}

// Runs on a worker thread. Borrows the socket owned by the task's shared_ptr.
void serve_client(const net::Socket& client) {
    int now = ++g_active_clients;
    std::cout << "Client connected (fd " << client.fd() << "), active: " << now << '\n';

    try {
        net::set_recv_timeout(client, kIdleTimeout);
        serve_http(client);
    } catch (const std::exception& e) {
        std::cerr << "client error: " << e.what() << '\n';
    }

    now = --g_active_clients;
    std::cout << "Client disconnected (fd " << client.fd() << "), active: " << now << '\n';
}

}  // namespace

int main() {
    try {
        unsigned int hw = std::thread::hardware_concurrency();
        std::size_t worker_count = (hw > 0) ? hw : 4;

        util::ThreadPool pool(worker_count);
        auto listener = net::listen_tcp(8080);

        std::cout << "HTTP server listening on http://127.0.0.1:8080 with " << worker_count
                  << " workers\n";

        while (true) {
            int fd = ::accept(listener.fd(), nullptr, nullptr);

            if (fd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "accept: " << errno_message(errno) << '\n';
                continue;
            }

            auto client = std::make_shared<net::Socket>(fd);

            if (!pool.submit([client] { serve_client(*client); })) {
                std::cerr << "pool is shutting down, dropping client\n";
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }
}