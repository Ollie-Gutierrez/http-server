#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

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
    const HeaderField* begin() const noexcept { return items_.data(); }
    const HeaderField* end() const noexcept { return items_.data() + n_; }

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
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    std::string serialize(bool keep_alive) const {
        std::string s;
        s.reserve(256 + body.size());
        s += "HTTP/1.1 ";
        s += std::to_string(status);
        s += ' ';
        s += reason;
        s += "\r\n";
        for (const auto& [k, v] : headers) {
            s += k;
            s += ": ";
            s += v;
            s += "\r\n";
        }
        s += "Content-Length: ";
        s += std::to_string(body.size());
        s += "\r\nConnection: ";
        s += keep_alive ? "keep-alive" : "close";
        s += "\r\n\r\n";
        s += body;
        return s;
    }
};
