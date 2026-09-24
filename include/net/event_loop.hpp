#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <sys/epoll.h>

#include "socket.hpp"

namespace net {

// A thin RAII wrapper around a Linux epoll instance.
class EventLoop {
  public:
    explicit EventLoop(std::size_t max_events = 256);

    void add(int fd, std::uint32_t events);     // start watching fd
    void modify(int fd, std::uint32_t events);  // change what we watch for
    void remove(int fd);                        // stop watching fd

    // Blocks until at least one fd is ready, or timeout_ms passes (-1 = forever).
    // The returned span is valid until the next call to wait().
    std::span<const epoll_event> wait(int timeout_ms);

  private:
    Socket epfd_;                      // an epoll instance IS an fd, so RAII applies
    std::vector<epoll_event> events_;  // reused buffer the kernel fills
};

}  // namespace net