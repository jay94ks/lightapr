#include "http_parsing.hpp"
#include <cctype>

namespace apr {

namespace {

bool iequals_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Splits `raw` into lines on '\n', stripping a trailing '\r' from each line.
std::string next_line(const std::string& raw, size_t& pos) {
    auto nl = raw.find('\n', pos);
    std::string line = (nl == std::string::npos) ? raw.substr(pos) : raw.substr(pos, nl - pos);
    pos = (nl == std::string::npos) ? raw.size() : nl + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

} // namespace

std::unordered_map<std::string, std::string> parse_query(const std::string& query_str) {
    std::unordered_map<std::string, std::string> result;
    size_t pos = 0;
    while (pos < query_str.size()) {
        auto amp = query_str.find('&', pos);
        size_t item_end = (amp == std::string::npos) ? query_str.size() : amp;
        std::string item = query_str.substr(pos, item_end - pos);

        auto eq_pos = item.find('=');
        if (eq_pos != std::string::npos) {
            result[item.substr(0, eq_pos)] = item.substr(eq_pos + 1);
        } else if (!item.empty()) {
            result[item] = "";
        }

        pos = (amp == std::string::npos) ? query_str.size() : amp + 1;
    }
    return result;
}

http_request parse_raw_http(const std::string& raw) {
    http_request req;
    size_t pos = 0;

    if (!raw.empty()) {
        std::string line = next_line(raw, pos);
        auto sp1 = line.find(' ');
        if (sp1 != std::string::npos) {
            req.method = line.substr(0, sp1);
            auto path_start = line.find_first_not_of(' ', sp1);
            std::string rest = (path_start == std::string::npos) ? "" : line.substr(path_start);

            auto sp2 = rest.find(' ');
            std::string full_path = (sp2 == std::string::npos) ? rest : rest.substr(0, sp2);
            if (sp2 != std::string::npos) {
                auto ver_start = rest.find_first_not_of(' ', sp2);
                if (ver_start != std::string::npos) req.http_version = rest.substr(ver_start);
            }

            auto q_pos = full_path.find('?');
            if (q_pos != std::string::npos) {
                req.path = full_path.substr(0, q_pos);
                req.query_params = parse_query(full_path.substr(q_pos + 1));
            } else {
                req.path = full_path;
            }
        }
    }

    while (pos < raw.size()) {
        std::string line = next_line(raw, pos);
        if (line.empty()) break; // End of headers
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string k = line.substr(0, colon);
            std::string v = line.substr(colon + 1);
            while (!v.empty() && v.front() == ' ') v.erase(0, 1);
            req.headers[k] = v;
        }
    }

    // HTTP/1.1 defaults to keep-alive unless the client asks to close;
    // HTTP/1.0 (and anything else) defaults to close unless the client asks
    // to keep the connection alive.
    std::string connection_value;
    for (const auto& [k, v] : req.headers) {
        if (iequals_ascii(k, "Connection")) {
            connection_value = v;
            break;
        }
    }
    std::string conn_lower = to_lower(connection_value);
    bool is_http11 = req.http_version.find("1.1") != std::string::npos;
    if (conn_lower == "close") {
        req.keep_alive = false;
    } else if (conn_lower == "keep-alive") {
        req.keep_alive = true;
    } else {
        req.keep_alive = is_http11;
    }

    return req;
}

std::optional<size_t> find_request_boundary(std::string_view buffered) {
    auto header_end = buffered.find("\r\n\r\n");
    if (header_end == std::string_view::npos) return std::nullopt;
    size_t headers_len = header_end + 4;

    size_t content_length = 0;
    std::string_view header_block = buffered.substr(0, header_end);
    size_t pos = 0;
    while (true) {
        auto nl = header_block.find('\n', pos);
        std::string_view line = (nl == std::string_view::npos) ? header_block.substr(pos) : header_block.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            std::string_view key = line.substr(0, colon);
            if (iequals_ascii(key, "Content-Length")) {
                std::string_view val = line.substr(colon + 1);
                while (!val.empty() && val.front() == ' ') val.remove_prefix(1);
                size_t parsed = 0;
                for (char c : val) {
                    if (c < '0' || c > '9') break;
                    parsed = parsed * 10 + static_cast<size_t>(c - '0');
                }
                content_length = parsed;
            }
        }

        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }

    size_t total = headers_len + content_length;
    if (buffered.size() < total) return std::nullopt;
    return total;
}

} // namespace apr
