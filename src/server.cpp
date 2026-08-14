#include "server.hpp"

#include "connection.hpp"
#include "http_types.hpp"
#include "request_parser.hpp"

#include <liburing.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

constexpr unsigned kRingDepth = 512;
constexpr std::size_t kReadChunk = 4096;
constexpr std::size_t kMaxReadBuf = 64 * 1024;  // unparseable garbage cap

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

    if (req.method != "GET") {
        resp.status = 405;
        resp.reason = "Method Not Allowed";
        resp.body = "405 method not allowed";
        return resp;
    }

    if (req.path.find("..") != std::string_view::npos) {
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
    resp.content_type = std::string(content_type_for(fp.string()));
    resp.body = std::move(body);
    return resp;
}

// low bits of user_data carry the op; Session* keeps those bits zero
enum class Op : std::uintptr_t { Accept = 0, Read = 1, Write = 2, Close = 3 };

struct Session;

std::uintptr_t tag(Session* s, Op op) {
    return reinterpret_cast<std::uintptr_t>(s) | static_cast<std::uintptr_t>(op);
}

struct Session {
    Connection conn;
    int fd_no;  // map key, stays valid after conn.release()
    std::string read_buf;
    std::string write_buf;
    std::size_t write_pos = 0;
    bool want_close = false;
};

// one ring per thread; unique_ptr sessions keep user_data pointers valid
// while ops are in flight
class Worker {
public:
    Worker(int listen_fd, const std::string& docroot) : listen_fd_(listen_fd), docroot_(docroot) {
        if (io_uring_queue_init(kRingDepth, &ring_, 0) < 0) {
            std::perror("io_uring_queue_init");
            std::exit(1);
        }
    }

    ~Worker() { io_uring_queue_exit(&ring_); }

    void run() {
        submit_accept();
        while (true) {
            io_uring_submit_and_wait(&ring_, 1);

            io_uring_cqe* cqe;
            unsigned head;
            unsigned count = 0;
            io_uring_for_each_cqe(&ring_, head, cqe) {
                handle(cqe);
                ++count;
            }
            if (count > 0) io_uring_cq_advance(&ring_, count);
        }
    }

private:
    // the SQ can fill mid-batch under a completion storm; flush and retry once
    io_uring_sqe* get_sqe() {
        io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            io_uring_submit(&ring_);
            sqe = io_uring_get_sqe(&ring_);
        }
        return sqe;
    }

    void submit_accept() {
        io_uring_sqe* sqe = get_sqe();
        io_uring_prep_multishot_accept(sqe, listen_fd_, nullptr, nullptr, 0);
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(tag(nullptr, Op::Accept)));
    }

    void submit_read(Session& s) {
        std::size_t have = s.read_buf.size();
        s.read_buf.resize(have + kReadChunk);
        io_uring_sqe* sqe = get_sqe();
        io_uring_prep_read(sqe, s.conn.fd(), s.read_buf.data() + have, kReadChunk, 0);
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(tag(&s, Op::Read)));
    }

    void submit_write(Session& s) {
        io_uring_sqe* sqe = get_sqe();
        io_uring_prep_write(sqe, s.conn.fd(), s.write_buf.data() + s.write_pos,
                            s.write_buf.size() - s.write_pos, 0);
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(tag(&s, Op::Write)));
    }

    // close through the ring: the fd must not be reused while the kernel knows it
    void submit_close(Session& s) {
        int fd = s.conn.release();
        io_uring_sqe* sqe = get_sqe();
        io_uring_prep_close(sqe, fd);
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(tag(&s, Op::Close)));
    }

    void drop(Session& s) { sessions_.erase(s.fd_no); }

    void handle(const io_uring_cqe* cqe) {
        auto bits = reinterpret_cast<std::uintptr_t>(io_uring_cqe_get_data(cqe));
        Session* s = reinterpret_cast<Session*>(bits & ~static_cast<std::uintptr_t>(3));
        switch (static_cast<Op>(bits & 3)) {
            case Op::Accept:
                on_accept(cqe->res, cqe->flags);
                break;
            case Op::Read:
                on_read(*s, cqe->res);
                break;
            case Op::Write:
                on_write(*s, cqe->res);
                break;
            case Op::Close:
                drop(*s);
                break;
        }
    }

    // no F_MORE means the multishot ended; re-arm only then
    void on_accept(int fd, unsigned flags) {
        if (fd < 0) {
            if (!(flags & IORING_CQE_F_MORE)) submit_accept();
            return;
        }

        auto s = std::make_unique<Session>();
        s->conn = Connection{fd};
        s->fd_no = fd;
        Session* raw = s.get();
        sessions_[fd] = std::move(s);
        submit_read(*raw);
    }

    void on_read(Session& s, int n) {
        if (n < 0) {
            drop(s);
            return;
        }
        if (n == 0) {  // peer closed; flush anything owed, then close
            s.want_close = true;
            if (!s.write_buf.empty()) {
                submit_write(s);
            } else {
                submit_close(s);
            }
            return;
        }
        s.read_buf.resize(s.read_buf.size() - (kReadChunk - static_cast<std::size_t>(n)));

        bool keep_alive = true;
        while (true) {
            ParseResult pr = parse_request(s.read_buf);
            if (!pr.complete) break;

            HttpResponse resp;
            if (pr.malformed) {
                resp.status = 400;
                resp.reason = "Bad Request";
                resp.body = "400 bad request";
                keep_alive = false;
            } else {
                keep_alive = !http::icontains(pr.request.header("connection"), "close");
                resp = handle_request(pr.request, docroot_);
            }
            resp.serialize_into(s.write_buf, keep_alive);
            s.read_buf.erase(0, pr.consumed);
            if (!keep_alive) break;
        }

        if (s.read_buf.size() > kMaxReadBuf) {
            HttpResponse resp;
            resp.status = 400;
            resp.reason = "Bad Request";
            resp.body = "400 bad request";
            resp.serialize_into(s.write_buf, false);
            keep_alive = false;
        }

        if (!keep_alive) s.want_close = true;
        if (!s.write_buf.empty()) {
            submit_write(s);
        } else if (s.want_close) {
            submit_close(s);
        } else {
            submit_read(s);
        }
    }

    void on_write(Session& s, int n) {
        if (n <= 0) {
            drop(s);
            return;
        }
        s.write_pos += static_cast<std::size_t>(n);
        if (s.write_pos < s.write_buf.size()) {
            submit_write(s);
            return;
        }
        s.write_buf.clear();
        s.write_pos = 0;
        if (s.want_close) {
            submit_close(s);
        } else {
            submit_read(s);
        }
    }

    io_uring ring_{};
    int listen_fd_;
    const std::string& docroot_;
    std::unordered_map<int, std::unique_ptr<Session>> sessions_;
};

int make_listener(int port) {
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::perror("socket");
        return -1;
    }

    int yes = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));

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

}  // namespace

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
        workers.emplace_back([fd, &docroot] { Worker(fd, docroot).run(); });
    }
    int fd = make_listener(port);
    if (fd < 0) return;
    Worker(fd, docroot).run();
}
