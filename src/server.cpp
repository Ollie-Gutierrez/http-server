#include "server.hpp"

#include "connection.hpp"
#include "http_types.hpp"
#include "request_parser.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kMaxEvents = 128;
constexpr std::size_t kReadChunk = 8192;

void set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

std::string_view content_type_for(std::string_view path) {
    auto dot = path.rfind('.');
    if (dot == std::string_view::npos) return "application/octet-stream";
    auto ext = path.substr(dot + 1);
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "txt") return "text/plain";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "svg") return "image/svg+xml";
    return "application/octet-stream";
}

HttpResponse handle_request(const HttpRequest& req, const std::string& docroot) {
    HttpResponse resp;
    resp.headers["Content-Type"] = "text/plain";

    if (req.method != "GET") {
        resp.status = 405;
        resp.reason = "Method Not Allowed";
        resp.body = "405 method not allowed";
        return resp;
    }

    if (req.path.find("..") != std::string::npos) {
        resp.status = 400;
        resp.reason = "Bad Request";
        resp.body = "400 bad request";
        return resp;
    }

    if (req.path == "/hello") {
        resp.status = 200;
        resp.reason = "OK";
        resp.body = "hello";
        return resp;
    }

    std::filesystem::path fp =
        std::filesystem::path(docroot) / (req.path == "/" ? "index.html" : req.path.substr(1));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(fp, ec)) {
        resp.status = 404;
        resp.reason = "Not Found";
        resp.body = "404 not found";
        return resp;
    }

    std::ifstream in(fp, std::ios::binary);
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    resp.status = 200;
    resp.reason = "OK";
    resp.headers["Content-Type"] = std::string(content_type_for(fp.string()));
    resp.body = std::move(body);
    return resp;
}

struct Session {
    Connection conn;
    std::string read_buf;
    std::size_t read_pos = 0;  // bytes of read_buf already parsed
    std::string write_buf;
    std::size_t write_pos = 0;
    bool want_close = false;
};

bool has_pending_writes(const Session& s) { return s.write_pos < s.write_buf.size(); }

void arm(int epfd, int fd, std::uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    ::epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

bool flush(int fd, Session& s) {
    while (has_pending_writes(s)) {
        ssize_t n = ::write(fd, s.write_buf.data() + s.write_pos, s.write_buf.size() - s.write_pos);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            if (errno == EINTR) continue;
            return false;
        }
        s.write_pos += static_cast<std::size_t>(n);
    }
    s.write_buf.clear();
    s.write_pos = 0;
    return true;
}

bool finish(int epfd, int fd, Session& s) {
    if (!flush(fd, s)) return false;
    std::uint32_t events = EPOLLIN | EPOLLET;
    if (has_pending_writes(s)) events |= EPOLLOUT;
    arm(epfd, fd, events);
    return !(s.want_close && !has_pending_writes(s));
}

bool process_reads(int epfd, int fd, Session& s, const std::string& docroot) {
    char chunk[kReadChunk];
    while (true) {
        ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) {
            s.want_close = true;
            break;
        }
        s.read_buf.append(chunk, static_cast<std::size_t>(n));

        while (!s.want_close) {
            ParseResult pr = parse_request(std::string_view(s.read_buf).substr(s.read_pos));
            if (!pr.complete) break;

            HttpResponse resp;
            bool keep_alive;
            if (pr.malformed) {
                resp.status = 400;
                resp.reason = "Bad Request";
                resp.headers["Content-Type"] = "text/plain";
                resp.body = "400 bad request";
                keep_alive = false;
            } else {
                keep_alive = !http::to_lower(pr.request.header("connection")).contains("close");
                resp = handle_request(pr.request, docroot);
            }
            s.write_buf += resp.serialize(keep_alive);
            s.read_pos += pr.consumed;
            if (!keep_alive) s.want_close = true;
        }
        // buffer fully parsed: take it back without a memmove
        if (s.read_pos == s.read_buf.size()) {
            s.read_buf.clear();
            s.read_pos = 0;
        }
    }
    return finish(epfd, fd, s);
}

void close_session(int epfd, std::unordered_map<int, Session>& sessions, int fd) {
    ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    sessions.erase(fd);
}

void accept_all(int epfd, int listen_fd, std::unordered_map<int, Session>& sessions) {
    while (true) {
        int fd = ::accept(listen_fd, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR) continue;
            break;  // EAGAIN/EWOULDBLOCK or error: done with this batch
        }
        set_nonblocking(fd);
        sessions[fd].conn = Connection{fd};
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = fd;
        ::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    }
}

void handle_conn_event(int epfd, int fd, std::uint32_t events, const std::string& docroot,
                       std::unordered_map<int, Session>& sessions) {
    auto it = sessions.find(fd);
    if (it == sessions.end()) return;
    Session& s = it->second;

    if (events & (EPOLLERR | EPOLLHUP)) {
        close_session(epfd, sessions, fd);
        return;
    }

    bool alive = true;
    if (events & EPOLLIN) alive = process_reads(epfd, fd, s, docroot);
    if (alive && (events & EPOLLOUT)) alive = finish(epfd, fd, s);
    if (!alive) close_session(epfd, sessions, fd);
}

}  // namespace

int make_listener(int port) {
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::perror("socket");
        return -1;
    }

    int yes = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    set_nonblocking(listen_fd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        return -1;
    }
    if (::listen(listen_fd, SOMAXCONN) < 0) {
        std::perror("listen");
        return -1;
    }
    return listen_fd;
}

// One listener plus one epoll loop per thread. Every worker accepts on
// its own SO_REUSEPORT socket, so the kernel spreads incoming connections
// over them and no connection is ever touched by two threads.
void worker_loop(int listen_fd, const std::string& docroot) {
    int epfd = ::epoll_create1(0);
    if (epfd < 0) {
        std::perror("epoll_create1");
        return;
    }

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd;
    ::epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    std::unordered_map<int, Session> sessions;
    std::vector<epoll_event> events(kMaxEvents);

    while (true) {
        int n = ::epoll_wait(epfd, events.data(), kMaxEvents, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == listen_fd) {
                accept_all(epfd, listen_fd, sessions);
            } else {
                handle_conn_event(epfd, fd, events[i].events, docroot, sessions);
            }
        }
    }
}

void run_server(int port, const std::string& docroot) {
    std::signal(SIGPIPE, SIG_IGN);

    unsigned n = std::thread::hardware_concurrency();
    if (n == 0) n = 1;
    std::printf("listening on :%d (docroot %s), %u worker thread%s\n", port, docroot.c_str(), n,
                n == 1 ? "" : "s");

    std::vector<std::jthread> workers;
    for (unsigned i = 1; i < n; ++i) {
        int fd = make_listener(port);
        if (fd < 0) return;
        workers.emplace_back(worker_loop, fd, docroot);
    }
    int fd = make_listener(port);
    if (fd < 0) return;
    worker_loop(fd, docroot);
}
