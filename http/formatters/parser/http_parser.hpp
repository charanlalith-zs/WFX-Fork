#ifndef WFX_HTTP_PARSER_HPP
#define WFX_HTTP_PARSER_HPP

#include <string>
#include <sstream>
#include <vector>
#include <optional>

#include "http/headers/http_headers.hpp"
#include "http/constants/http_constants.hpp"

namespace WFX::Http {

struct HttpRequest {
    HttpMethod  method;
    HttpVersion version;
    HttpHeaders headers;
    std::string path;
    std::string body;
};

class HttpParser {
public:
    static std::optional<HttpRequest> Parse(const std::string& rawRequest);
};

} // namespace WFX

#endif // WFX_HTTP_PARSER_HPP