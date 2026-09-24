#include <gtest/gtest.h>

#include <fcntl.h>   // fcntl, F_GETFD
#include <unistd.h>  // pipe, close
#include <utility>   // std::move

#include "net/socket.hpp"

namespace {

// Is this fd currently open? fcntl(F_GETFD) fails with -1 on a closed fd.
bool is_open(int fd) {
    return ::fcntl(fd, F_GETFD) != -1;
}

// Helper: get one real, open fd without any networking.
// pipe() creates two connected fds; we close the write end and keep the read end.
int make_fd() {
    int fds[2];
    if (::pipe(fds) != 0) {
        return -1;
    }
    ::close(fds[1]);
    return fds[0];
}

}  // namespace

TEST(Socket, DefaultConstructedIsInvalid) {
    net::Socket s;
    EXPECT_FALSE(s.valid());
}

TEST(Socket, DestructorClosesFd) {
    int fd = make_fd();
    ASSERT_GE(fd, 0);  // ASSERT stops the test here if setup failed

    {
        net::Socket s{fd};
        EXPECT_TRUE(is_open(fd));
    }  // s is destroyed here -> should close fd

    EXPECT_FALSE(is_open(fd));
}

TEST(Socket, MoveConstructorTransfersOwnership) {
    int fd = make_fd();
    ASSERT_GE(fd, 0);

    net::Socket a{fd};
    net::Socket b{std::move(a)};

    EXPECT_FALSE(a.valid());  // a gave up ownership
    EXPECT_TRUE(b.valid());   // b owns it now
    EXPECT_EQ(b.fd(), fd);
}

TEST(Socket, MoveAssignmentClosesPreviousFd) {
    int fd1 = make_fd();
    int fd2 = make_fd();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    net::Socket a{fd1};
    net::Socket b{fd2};

    b = std::move(a);  // b must close fd2 before taking fd1

    EXPECT_FALSE(is_open(fd2));  // old fd released, not leaked
    EXPECT_EQ(b.fd(), fd1);
    EXPECT_FALSE(a.valid());
}

TEST(Socket, ListenTcpOnEphemeralPort) {
    // Port 0 = "kernel, pick any free port". Avoids clashes on CI machines.
    auto s = net::listen_tcp(0);
    EXPECT_TRUE(s.valid());
}