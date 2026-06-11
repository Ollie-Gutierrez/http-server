#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "http_types.hpp"

struct ParseResult {
    bool complete = false;
    bool malformed = false;
    std::size_t consumed = 0;
    HttpRequest request;
};

ParseResult parse_request(std::string_view buf);
