#include <gtest/gtest.h>

#include <cerrno>
#include <cstring>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net/event_loop.hpp"
#include "net/socket.hpp"

namespace {

// A connected pair of sockets, entirely in the kernel: no network needed.
struct SocketPair {
    net::Socket a;
    net::Socket b;
};

SocketPair make_pair() {
    int fds[2];
    EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    return SocketPair{net::Socket{fds[0]}, net::Socket{fds[1]}};
}

}  // namespace

TEST(EventLoop, TimesOutWhenNothingIsReady) {
    net::EventLoop loop;
    auto pair = make_pair();
    loop.add(pair.a.fd(), EPOLLIN);

    auto ready = loop.wait(20);  // nothing written, so nothing should be ready
    EXPECT_TRUE(ready.empty());
}

TEST(EventLoop, ReportsReadableFd) {
    net::EventLoop loop;
    auto pair = make_pair();
    loop.add(pair.a.fd(), EPOLLIN);

    ASSERT_EQ(::send(pair.b.fd(), "x", 1, 0), 1);

    auto ready = loop.wait(500);
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0].data.fd, pair.a.fd());
    EXPECT_NE(ready[0].events & EPOLLIN, 0u);
}

TEST(EventLoop, ModifyChangesInterest) {
    net::EventLoop loop;
    auto pair = make_pair();
    loop.add(pair.a.fd(), EPOLLOUT);  // watch for writable only

    ASSERT_EQ(::send(pair.b.fd(), "x", 1, 0), 1);

    auto ready = loop.wait(100);
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready[0].events & EPOLLIN, 0u);  // we never asked about readability

    loop.modify(pair.a.fd(), EPOLLIN);
    ready = loop.wait(100);
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_NE(ready[0].events & EPOLLIN, 0u);
}

TEST(EventLoop, RemoveStopsReporting) {
    net::EventLoop loop;
    auto pair = make_pair();
    loop.add(pair.a.fd(), EPOLLIN);
    loop.remove(pair.a.fd());

    ASSERT_EQ(::send(pair.b.fd(), "x", 1, 0), 1);
    EXPECT_TRUE(loop.wait(20).empty());
}

TEST(EventLoop, NonBlockingRecvReturnsEagain) {
    auto pair = make_pair();
    net::set_nonblocking(pair.a);

    char buf[16];
    ssize_t n = ::recv(pair.a.fd(), buf, sizeof(buf), 0);

    EXPECT_EQ(n, -1);
    EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
}