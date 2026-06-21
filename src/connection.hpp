#pragma once

#include <unistd.h>

class Connection {
public:
    Connection() = default;
    explicit Connection(int fd) : fd_(fd) {}
    ~Connection() {
        if (fd_ >= 0) ::close(fd_);
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int fd() const noexcept { return fd_; }
    int release() noexcept {
        int f = fd_;
        fd_ = -1;
        return f;
    }

private:
    int fd_ = -1;
};
