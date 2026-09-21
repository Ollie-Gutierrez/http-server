#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>

namespace http {

inline char ascii_lower(char c) {
    return 'A' <= c && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    }
    return true;
}

inline bool icontains(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        if (iequals(hay.substr(i, needle.size()), needle)) return true;
    }
    return false;
}

}  // namespace http

struct HeaderField {
    std::string_view name;
    std::string_view value;
};

// views into the read buffer; lines past kMax dropped
class HeaderList {
public:
    static constexpr std::size_t kMax = 32;

    void emplace(std::string_view name, std::string_view value) {
        if (n_ < kMax) items_[n_++] = HeaderField{name, value};
    }

    std::string_view find(std::string_view name) const {
        for (std::size_t i = 0; i < n_; ++i) {
            if (http::iequals(items_[i].name, name)) return items_[i].value;
        }
        return {};
    }

    std::size_t size() const noexcept { return n_; }
    void reset() noexcept { n_ = 0; }

private:
    std::array<HeaderField, kMax> items_;
    std::size_t n_ = 0;
};

struct HttpRequest {
    std::string_view method;
    std::string_view path;
    HeaderList headers;
    std::string body;  // only non-empty when a body was sent

    std::string_view header(std::string_view key) const { return headers.find(key); }
};

struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::string content_type = "text/plain";
    std::string body;

    void serialize_into(std::string& out, bool keep_alive) const {
        char status_buf[16];
        char length_buf[16];
        auto st = std::to_chars(status_buf, status_buf + sizeof status_buf, status);
        auto ln = std::to_chars(length_buf, length_buf + sizeof length_buf, body.size());

        out.reserve(out.size() + 160 + content_type.size() + body.size());
        out += "HTTP/1.1 ";
        out.append(status_buf, st.ptr);
        out += ' ';
        out += reason;
        out += "\r\nContent-Type: ";
        out += content_type;
        out += "\r\nContent-Length: ";
        out.append(length_buf, ln.ptr);
        out += "\r\nConnection: ";
        out += keep_alive ? "keep-alive" : "close";
        out += "\r\n\r\n";
        out += body;
    }
};

inline HttpResponse make_error(int status, const char* reason, const char* body) {
    HttpResponse r;
    r.status = status;
    r.reason = reason;
    r.body = body;
    return r;
}
