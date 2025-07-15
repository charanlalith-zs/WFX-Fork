#ifndef WFX_HTTP_RESPONSE_HPP
#define WFX_HTTP_RESPONSE_HPP

#include "http/constants/http_constants.hpp"
#include "http/headers/http_headers.hpp"

#include "third_party/nlohmann/json_fwd.hpp"

#include <variant>
#include <string>

// To keep naming consistent :)
using Json     = nlohmann::json;
using BodyType = std::variant<std::monostate, std::string_view, std::string>;

namespace WFX::Http {

struct HttpResponse {
    HttpVersion     version = HttpVersion::HTTP_1_1;
    HttpStatus      status  = HttpStatus::OK;
    ResponseHeaders headers;
    BodyType        body;

    HttpResponse& Status(HttpStatus code);
    HttpResponse& Set(const std::string& key, const std::string& value);
    bool IsFileOperation() const;

    void SendText(const char* cstr);
    void SendText(std::string_view view);
    void SendText(std::string&& str);

    void SendJson(const Json& j);
    void SendJson(Json&& j);

    void SendFile(const char* cstr);
    void SendFile(std::string_view path);
    void SendFile(std::string&& path);

private:
    void SetTextBody(std::string&& text, const char* contentType);
    void PrepareFileHeaders(std::string_view path);
    void ValidateFileSend(std::string_view path);

private:
    bool isFileOperation_ = false;
};

} // namespace WFX::Http

#endif // WFX_HTTP_RESPONSE_HPP