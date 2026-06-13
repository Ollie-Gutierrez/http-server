#include "server.hpp"

#include "http_types.hpp"
#include "request_parser.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

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

bool write_all(int fd, std::string_view data) {
    const char* p = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t n = ::write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
}

void handle_connection(int fd, const std::string& docroot) {
    std::string buf;
    char chunk[8192];
    while (true) {
        ParseResult pr;
        while (true) {
            pr = parse_request(buf);
            if (pr.complete) break;
            ssize_t n = ::read(fd, chunk, sizeof(chunk));
            if (n < 0) {
                if (errno == EINTR) continue;
                return;
            }
            if (n == 0) return;
            buf.append(chunk, static_cast<std::size_t>(n));
        }

        if (pr.malformed) {
            HttpResponse r;
            r.status = 400;
            r.reason = "Bad Request";
            r.headers["Content-Type"] = "text/plain";
            r.body = "400 bad request";
            write_all(fd, r.serialize(false));
            return;
        }

        bool keep_alive = !http::to_lower(pr.request.header("connection")).contains("close");
        HttpResponse resp = handle_request(pr.request, docroot);
        if (!write_all(fd, resp.serialize(keep_alive))) return;

        buf.erase(0, pr.consumed);
        if (!keep_alive) return;
    }
}

}  // namespace

void run_server(int port, const std::string& docroot) {
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::perror("socket");
        return;
    }

    int yes = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        return;
    }
    if (::listen(listen_fd, SOMAXCONN) < 0) {
        std::perror("listen");
        return;
    }

    std::printf("listening on :%d (docroot %s)\n", port, docroot.c_str());

    while (true) {
        int conn_fd = ::accept(listen_fd, nullptr, nullptr);
        if (conn_fd < 0) {
            if (errno == EINTR) continue;
            std::perror("accept");
            continue;
        }
        handle_connection(conn_fd, docroot);
        ::close(conn_fd);
    }
}
