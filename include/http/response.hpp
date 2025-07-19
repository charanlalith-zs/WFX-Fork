#ifndef WFX_INC_HTTP_USER_RESPONSE_HPP
#define WFX_INC_HTTP_USER_RESPONSE_HPP

#include "shared/apis/http_api.hpp"

// Forward declare HttpResponse
namespace WFX::Http {
    struct HttpResponse;
}

/* User side implementation of 'Response' class. Engine passes the API */
class Response {
public:
    Response(WFX::Http::HttpResponse* backend, const WFX::Shared::HTTP_API_TABLE* httpApi)
        : backend_(backend), httpApi_(httpApi)
    {}

    Response& Status(WFX::Http::HttpStatus code)
    {
        httpApi_->SetStatus(backend_, code);
        return *this;
    }

    Response& Set(std::string key, std::string value)
    {
        httpApi_->SetHeader(backend_, std::move(key), std::move(value));
        return *this;
    }

    // SendText overloads
    void SendText(const char* cstr)      { httpApi_->SendTextCStr(backend_, cstr); }
    void SendText(std::string_view view) { httpApi_->SendTextView(backend_, view); }
    void SendText(std::string&& str)     { httpApi_->SendTextMove(backend_, std::move(str)); }

    // SendJson overloads
    void SendJson(const Json& j)         { httpApi_->SendJsonConstRef(backend_, &j); }
    void SendJson(Json&& j)              { httpApi_->SendJsonMove(backend_, std::move(j)); }

    // SendFile overloads
    void SendFile(const char* path)      { httpApi_->SendFileCStr(backend_, path); }
    void SendFile(std::string_view path) { httpApi_->SendFileView(backend_, path); }
    void SendFile(std::string&& path)    { httpApi_->SendFileMove(backend_, std::move(path)); }

    bool IsFileOperation() const         { return httpApi_->IsFileOperation(backend_); }

private:
    WFX::Http::HttpResponse*           backend_;
    const WFX::Shared::HTTP_API_TABLE* httpApi_;
};

#endif // WFX_INC_HTTP_USER_RESPONSE_HPP