#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kResponse =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 5\r\n"
    "Connection: close\r\n"
    "\r\n"
    "hello";

std::string read_headers(int fd) {
    std::string buf;
    char chunk[4096];
    while (true) {
        ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n <= 0) break;
        buf.append(chunk, static_cast<size_t>(n));
        if (buf.find("\r\n\r\n") != std::string::npos) break;
    }
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 8080;

    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::perror("socket");
        return 1;
    }

    int yes = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        return 1;
    }
    if (::listen(listen_fd, SOMAXCONN) < 0) {
        std::perror("listen");
        return 1;
    }

    std::printf("listening on :%d\n", port);

    while (true) {
        int conn_fd = ::accept(listen_fd, nullptr, nullptr);
        if (conn_fd < 0) {
            if (errno == EINTR) continue;
            std::perror("accept");
            continue;
        }

        read_headers(conn_fd);
        std::string resp(kResponse);
        (void)::write(conn_fd, resp.data(), resp.size());
        ::close(conn_fd);
    }
}
