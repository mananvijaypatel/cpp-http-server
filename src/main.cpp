#include <atomic>          // std::atomic
#include <cerrno>          // errno
#include <cstddef>         // std::size_t
#include <exception>       // std::exception
#include <iostream>        // std::cout, std::cerr
#include <memory>          // std::make_shared
#include <string>          // std::string
#include <string_view>     // std::string_view
#include <system_error>    // std::generic_category
#include <thread>          // std::thread::hardware_concurrency

#include <sys/socket.h>    // accept, recv
#include <sys/types.h>     // ssize_t

#include "net/socket.hpp"
#include "util/thread_pool.hpp"

namespace {

// Shared by all worker threads; atomic so ++/-- are race-free.
std::atomic<int> g_active_clients{0};

// Thread-safe replacement for std::strerror.
std::string errno_message(int err) {
    return std::generic_category().message(err);
}

// Echo bytes back until the client disconnects.
void echo_loop(const net::Socket& client) {
    char buf[4096];

    while (true) {
        ssize_t n = ::recv(client.fd(), buf, sizeof(buf), 0);

        if (n > 0) {
            net::send_all(client, std::string_view(buf, static_cast<std::size_t>(n)));
        } else if (n == 0) {
            return;   // client closed the connection
        } else {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "recv: " << errno_message(errno) << '\n';
            return;
        }
    }
}

// Runs on a worker thread from the pool.
// Borrows the socket: the shared_ptr captured in the task lambda owns it.
void serve_client(const net::Socket& client) {
    int now = ++g_active_clients;
    std::cout << "Client connected (fd " << client.fd()
              << "), active: " << now << '\n';

    try {
        echo_loop(client);
    } catch (const std::exception& e) {
        std::cerr << "client error: " << e.what() << '\n';
    }

    now = --g_active_clients;
    std::cout << "Client disconnected (fd " << client.fd()
              << "), active: " << now << '\n';
}

} // namespace

int main() {
    try {
        // One worker per CPU core; fall back to 4 if the count is unknown.
        unsigned int hw = std::thread::hardware_concurrency();
        std::size_t worker_count = (hw > 0) ? hw : 4;

        util::ThreadPool pool(worker_count);
        auto listener = net::listen_tcp(8080);

        std::cout << "Echo server listening on 127.0.0.1:8080 with "
                  << worker_count << " workers\n";

        while (true) {
            int fd = ::accept(listener.fd(), nullptr, nullptr);

            if (fd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "accept: " << errno_message(errno) << '\n';
                continue;
            }

            // std::function needs a copyable callable, and Socket is move-only,
            // so the task holds the socket through a shared_ptr. The fd closes
            // when the task finishes and the last shared_ptr is released.
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