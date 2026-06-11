#include "request_parser.hpp"

#include <charconv>
#include <cstddef>
#include <system_error>

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

}  // namespace

ParseResult parse_request(std::string_view buf) {
    ParseResult r;

    auto end_headers = buf.find("\r\n\r\n");
    if (end_headers == std::string_view::npos) return r;

    std::string_view head = buf.substr(0, end_headers);
    std::size_t header_block_len = end_headers + 4;

    auto eol = head.find("\r\n");
    std::string_view request_line = eol == std::string_view::npos ? head : head.substr(0, eol);

    auto sp1 = request_line.find(' ');
    auto sp2 = request_line.find(' ', sp1 + 1);
    if (sp1 == std::string_view::npos || sp2 == std::string_view::npos) {
        r.malformed = true;
        r.complete = true;
        r.consumed = header_block_len;
        return r;
    }
    r.request.method = std::string(request_line.substr(0, sp1));
    r.request.path = std::string(request_line.substr(sp1 + 1, sp2 - sp1 - 1));

    std::size_t pos = eol == std::string_view::npos ? head.size() : eol + 2;
    while (pos < head.size()) {
        auto next = head.find("\r\n", pos);
        std::string_view line =
            next == std::string_view::npos ? head.substr(pos) : head.substr(pos, next - pos);
        if (line.empty()) break;

        auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            r.malformed = true;
            r.complete = true;
            r.consumed = header_block_len;
            return r;
        }
        std::string key = http::to_lower(trim(line.substr(0, colon)));
        std::string val = std::string(trim(line.substr(colon + 1)));
        r.request.headers.emplace(std::move(key), std::move(val));

        if (next == std::string_view::npos) break;
        pos = next + 2;
    }

    std::size_t body_len = 0;
    if (auto cl = r.request.header("content-length"); !cl.empty()) {
        long long len = 0;
        auto [p, ec] = std::from_chars(cl.data(), cl.data() + cl.size(), len);
        if (ec != std::errc{} || len < 0 || p != cl.data() + cl.size()) {
            r.malformed = true;
            r.complete = true;
            r.consumed = header_block_len;
            return r;
        }
        body_len = static_cast<std::size_t>(len);
    }

    if (buf.size() < header_block_len + body_len) return r;

    r.request.body = std::string(buf.substr(header_block_len, body_len));
    r.consumed = header_block_len + body_len;
    r.complete = true;
    return r;
}
