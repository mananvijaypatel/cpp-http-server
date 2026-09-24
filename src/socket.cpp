#include "net/socket.hpp"

#include <cerrno>        // errno
#include <system_error>  // std::system_error
#include <utility>       // std::exchange

#include <netinet/in.h>  // sockaddr_in, htons, htonl, INADDR_LOOPBACK
#include <sys/socket.h>  // socket, setsockopt, bind, listen, send
#include <sys/types.h>   // ssize_t
#include <unistd.h>      // close

#include <sys/time.h>  // for timeval

namespace net {

Socket::Socket(int fd) noexcept : fd_(fd) {}

Socket::~Socket() {
    if (valid()) {     // only close if we actually own something (fd_ >= 0)
        ::close(fd_);  // :: means "the global C function close", not a member
    }
}

// Move constructor: "steal" the fd from another Socket
// exchange does 2 things
//      1. returns other's current fd -> we store it in our fd_
//      2. sets other.fd_ to -1       -> other now owns nothing
// Result: exactly one object owns the fd, so it is closed exactly once
Socket::Socket(Socket&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

// Move assignment: "I may already own an fd, and I'm being given a new one"
Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {  // guard: s = std::move(s) must be safe
        if (valid()) {
            ::close(fd_);  // release OUR old fd first, or it leaks
        }
        fd_ = std::exchange(other.fd_, -1);  // take theirs, leave them empty
    }
    return *this;  // lets you chain: a = b = std::move(c)
}

Socket listen_tcp(std::uint16_t port, int backlog) {
    // 1. Create a TCP socket and give it to an owner immediately.
    //    If any later step throws, sock's destructor closes the fd for us.
    Socket sock{::socket(AF_INET, SOCK_STREAM, 0)};
    if (!sock.valid()) {
        throw std::system_error(errno, std::generic_category(), "socket");
    }

    // 2. Allow fast restarts even if old connections are in TIME_WAIT.
    //    Must be set before bind().
    int yes = 1;
    if (::setsockopt(sock.fd(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        throw std::system_error(errno, std::generic_category(), "setsockopt(SO_REUSEADDR)");
    }

    // 3. Attach to 127.0.0.1:port
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(sock.fd(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::system_error(errno, std::generic_category(), "bind");
    }

    // 4. Start accepting connections. backlog = max queued connections.
    if (::listen(sock.fd(), backlog) < 0) {
        throw std::system_error(errno, std::generic_category(), "listen");
    }

    return sock;  // Socket can't be copied, so this moves it out (uses our move ctor)
}

void send_all(const Socket& s, std::string_view data) {
    const char* ptr = data.data();        // where the unsent bytes start
    std::size_t remaining = data.size();  // how many bytes are still unsent

    while (remaining > 0) {
        // send() may send FEWER bytes than asked. It returns how many it sent.
        // MSG_NOSIGNAL: return an error instead of killing the server with SIGPIPE.
        ssize_t n = ::send(s.fd(), ptr, remaining, MSG_NOSIGNAL);

        if (n < 0) {
            if (errno == EINTR) {
                continue;  // interrupted by a signal: just retry
            }
            throw std::system_error(errno, std::generic_category(), "send");
        }

        ptr += n;                                  // skip past what was sent
        remaining -= static_cast<std::size_t>(n);  // fewer bytes left
    }
}

void set_recv_timeout(const Socket& s, std::chrono::milliseconds timeout) {
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);

    if (::setsockopt(s.fd(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        throw std::system_error(errno, std::generic_category(), "setsockopt(SO_RCVTIME)");
    }
}

}  // namespace net