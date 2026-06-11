#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>

namespace http {

inline std::string to_lower(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

}  // namespace http

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    std::string_view header(std::string_view key) const {
        auto it = headers.find(http::to_lower(key));
        return it == headers.end() ? std::string_view{} : std::string_view{it->second};
    }
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
