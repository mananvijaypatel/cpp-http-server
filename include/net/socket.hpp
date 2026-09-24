#pragma once
#include <cstdint>
#include <string_view>

#include <chrono>

// Why this design:

// No copying. If two Socket objects held the same fd, both destructors would call close() on it.
// That's a double-close bug, and it can silently close a different connection that reused the same
// fd number. Moving is allowed, which transfers ownership. For example, listen_tcp can return a
// Socket, and later you can hand a client socket to a worker thread. noexcept on moves and the
// destructor guarantees they never throw. Standard containers like std::vector rely on this to move
// elements safely instead of copying them.

// How it works: Deleting the copy constructor and copy assignment makes copying a compile error.
// The move constructor takes other.fd_ and sets other.fd_ = -1, so only one object ever owns the
// fd. This set of special member functions is called the Rule of Five.

// In industry: This exact pattern appears everywhere a resource must be released: std::unique_ptr,
// std::fstream, database connection handles, mutex locks (std::lock_guard), and GPU buffers. Every
// serious C++ codebase has its own version of this Socket or FileDescriptor class. Being able to
// write one correctly is a common interview question.

namespace net {
// Owns a file descriptor and closes it automatically (RAII).
class Socket {
  public:
    Socket() = default;                // owns nothing (fd_ = -1)
    explicit Socket(int fd) noexcept;  // takes ownership of fd
    ~Socket();                         // closes fd if valid

    Socket(const Socket&) = delete;  // no copying
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept;  // moving transfers ownership
    Socket& operator=(Socket&& other) noexcept;

    // int fd() const noexcept {
    //     return fd_;
    // }
    // bool valid() const noexcept {
    //     return fd_ >= 0;
    // }

    [[nodiscard]] int fd() const noexcept {
        return fd_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return fd_ >= 0;
    }

  private:
    int fd_ = -1;
};

// Creates a TCP socket listening on localhost:port.
// Throws std::system_error on failure
Socket listen_tcp(std::uint16_t port, int backlog = 128);

// Sends every byte of data, looping over partial sends.
// Throws std::system_error on failure
void send_all(const Socket& s, std::string_view data);

// new declaration
// make recv() fail with EAGAIN if no data arrives within `timeout`.
void set_recv_timeout(const Socket& s, std::chrono::milliseconds timeout);

}  // namespace net
