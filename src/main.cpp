#include <cerrno>          // errno
#include <cstring>         // std::strerror: turns errno into readable text
#include <iostream>        // std::cout, std::cerr
#include <string_view>
#include <system_error>

#include <sys/socket.h>    // accept, recv
#include <sys/types.h>     // ssize_t

#include "net/socket.hpp"

namespace {   // anonymous namespace: this function is private to main.cpp

// Talk to one client until it disconnects: read bytes, send them back.
void handle_client(const net::Socket& client) {
    char buf[4096];   // 4 KB buffer: a common size (matches a memory page)

    while (true) {
        // recv() waits until data arrives, then copies up to sizeof(buf) bytes.
        ssize_t n = ::recv(client.fd(), buf, sizeof(buf), 0);

        if (n > 0) {
            // Got n bytes: echo exactly those bytes back (not the whole buffer!)
            net::send_all(client, std::string_view(buf, static_cast<std::size_t>(n)));
        } else if (n == 0) {
            return;   // 0 = client closed the connection normally (NOT an error)
        } else {
            if (errno == EINTR) {
                continue;   // interrupted by a signal: try again
            }
            std::cerr << "recv: " << std::strerror(errno) << '\n';
            return;         // real error: give up on this client
        }
    }
}

} // namespace

int main() {
    try {
        // Create the listening socket (throws if the port is busy, etc.)
        auto listener = net::listen_tcp(8080);
        std::cout << "Echo server listening on 127.0.0.1:8080\n";

        while (true) {
            // accept() waits for a client and returns a NEW fd just for that client.
            // nullptr, nullptr = we don't need the client's address yet.
            int fd = ::accept(listener.fd(), nullptr, nullptr);

            if (fd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "accept: " << std::strerror(errno) << '\n';
                continue;   // one failed accept shouldn't kill the whole server
            }

            net::Socket client{fd};   // hand the fd to an owner immediately
            std::cout << "Client connected (fd " << client.fd() << ")\n";

            try {
                handle_client(client);
            } catch (const std::system_error& e) {
                // e.g. send failed because the client vanished.
                // Log it and move on: one bad client must not crash the server.
                std::cerr << "client error: " << e.what() << '\n';
            }

            std::cout << "Client disconnected\n";
        }   // <- client's destructor runs here and closes its fd automatically

    } catch (const std::system_error& e) {
        // Startup failed (socket/bind/listen): nothing to recover, so exit.
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;   // non-zero exit code = failure (scripts and systemd check this)
    }
}