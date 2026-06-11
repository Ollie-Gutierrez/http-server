#include "request_parser.hpp"

#include <gtest/gtest.h>

TEST(Parser, SimpleGet) {
    std::string raw = "GET /hello HTTP/1.1\r\nHost: example.com\r\n\r\n";
    auto r = parse_request(raw);
    ASSERT_TRUE(r.complete);
    EXPECT_FALSE(r.malformed);
    EXPECT_EQ(r.consumed, raw.size());
    EXPECT_EQ(r.request.method, "GET");
    EXPECT_EQ(r.request.path, "/hello");
    EXPECT_TRUE(r.request.body.empty());
}

TEST(Parser, ContentLengthBody) {
    std::string raw = "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello";
    auto r = parse_request(raw);
    ASSERT_TRUE(r.complete);
    EXPECT_FALSE(r.malformed);
    EXPECT_EQ(r.consumed, raw.size());
    EXPECT_EQ(r.request.body, "hello");
}

TEST(Parser, TruncatedBody) {
    std::string raw = "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhe";
    auto r = parse_request(raw);
    EXPECT_FALSE(r.complete);
    EXPECT_FALSE(r.malformed);
}

TEST(Parser, PartialHeaders) {
    std::string raw = "GET / HTTP/1.1\r\nHost: x";
    auto r = parse_request(raw);
    EXPECT_FALSE(r.complete);
}

TEST(Parser, HeaderLookupCaseInsensitive) {
    std::string raw = "GET / HTTP/1.1\r\nContent-Type: text/plain\r\n\r\n";
    auto r = parse_request(raw);
    ASSERT_TRUE(r.complete);
    EXPECT_EQ(r.request.header("content-type"), "text/plain");
    EXPECT_EQ(r.request.header("CONTENT-TYPE"), "text/plain");
    EXPECT_TRUE(r.request.header("missing").empty());
}

TEST(Parser, PipelinedLeavesRemainder) {
    std::string raw = "GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\r\nHost: x\r\n\r\n";
    auto r = parse_request(raw);
    ASSERT_TRUE(r.complete);
    EXPECT_EQ(r.request.path, "/a");
    EXPECT_EQ(r.consumed, raw.size() / 2);
}

TEST(Parser, MalformedRequestLine) {
    std::string raw = "garbage\r\n\r\n";
    auto r = parse_request(raw);
    EXPECT_TRUE(r.malformed);
}

TEST(Parser, InvalidContentLength) {
    std::string raw = "POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n";
    auto r = parse_request(raw);
    EXPECT_TRUE(r.malformed);
}
