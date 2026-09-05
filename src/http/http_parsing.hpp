#ifndef APR_HTTP_PARSING_HPP
#define APR_HTTP_PARSING_HPP

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace apr {

struct http_request {
    std::string method;
    std::string path;
    std::string http_version{"HTTP/1.1"};
    std::unordered_map<std::string, std::string> query_params;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    bool keep_alive{true};
};

struct http_response {
    int status_code{200};
    std::string status_text{"OK"};
    std::string content_type{"application/json"};
    std::string body;
};

// Pure parsing helpers, factored out so they're unit-testable without a live
// socket. All operate on already-buffered bytes; none perform I/O.

std::unordered_map<std::string, std::string> parse_query(const std::string& query_str);

http_request parse_raw_http(const std::string& raw);

// Returns the total byte length (header block + Content-Length body, if any)
// of one complete request sitting at the front of `buffered`, or
// std::nullopt if more bytes are still needed to complete it. A request with
// no recognized Content-Length is assumed to have no body (this server has
// no route that reads a request body today).
std::optional<size_t> find_request_boundary(std::string_view buffered);

} // namespace apr

#endif // APR_HTTP_PARSING_HPP
