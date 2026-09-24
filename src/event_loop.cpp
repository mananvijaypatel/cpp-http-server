#include "../include/net/event_loop.hpp"

#include <cerrno>
#include <system_error>

namespace net {
EventLoop::EventLoop(std::size_t max_events)
    : epfd_(::epoll_create1(EPOLL_CLOEXEC)), events_(max_events) {
    if (!epfd_.valid()) {
        throw std::system_error(errno, std::generic_category(), "epoll_create1");
    }
}

void EventLoop::add(int fd, std::uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;  // the kernel hands this back to us in wait()
    if (::epoll_ctl(epfd_.fd(), EPOLL_CTL_ADD, fd, &ev) < 0) {
        throw std::system_error(errno, std::generic_category(), "epoll_ctl(ADD)");
    }
}

void EventLoop::modify(int fd, std::uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (::epoll_ctl(epfd_.fd(), EPOLL_CTL_MOD, fd, &ev) < 0) {
        throw std::system_error(errno, std::generic_category(), "epoll_ctl(MOD)");
    }
}

void EventLoop::remove(int fd) {
    // Closing an fd removes it from epoll automatically, but being explicit
    // keeps the intent clear and catches bookkeeping mistakes.
    if (::epoll_ctl(epfd_.fd(), EPOLL_CTL_DEL, fd, nullptr) < 0 && errno != ENOENT &&
        errno != EBADF) {
        throw std::system_error(errno, std::generic_category(), "epoll_ctl(DEL)");
    }
}

std::span<const epoll_event> EventLoop::wait(int timeout_ms) {
    int n = ::epoll_wait(epfd_.fd(), events_.data(), static_cast<int>(events_.size()), timeout_ms);
    if (n < 0) {
        if (errno == EINTR) {
            return {};  // interrupted by a signal: treat as "nothing ready"
        }
        throw std::system_error(errno, std::generic_category(), "epoll_wait");
    }
    return {events_.data(), static_cast<std::size_t>(n)};
}

}  // namespace net