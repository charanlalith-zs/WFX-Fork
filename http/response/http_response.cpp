#include "http_response.hpp"

#include "http/common/http_detector.hpp"
#include "utils/filesystem/filesystem.hpp"
#include "utils/backport/string.hpp"
#include "utils/logger/logger.hpp"

#include "third_party/nlohmann/json.hpp"

#include <type_traits>
#include <charconv>

namespace WFX::Http {

using namespace WFX::Utils; // For 'Logger', 'FileSystem'

constexpr const char* CONTENT_TYPE_PLAIN = "text/plain";
constexpr const char* CONTENT_TYPE_JSON  = "application/json";

HttpResponse& HttpResponse::Status(HttpStatus code)
{
    status = code;
    return *this;
}

HttpResponse& HttpResponse::Set(const std::string& key, const std::string& value)
{
    headers.SetHeader(key, value);
    return *this;
}

bool HttpResponse::IsFileOperation() const
{
    return isFileOperation_;
}

// vvv MAIN SHIT BELOW vvv
// vvv TEXT vvv
void HttpResponse::SendText(const char* cstr)
{
    SendText(std::string_view{cstr});
}

void HttpResponse::SendText(std::string_view view)
{
    auto& logger = Logger::GetInstance();
    
    if(!std::holds_alternative<std::monostate>(body))
        logger.Fatal("[HttpResponse]: SendText() called after response body already set");
    
    if(isFileOperation_)
        logger.Fatal("[HttpResponse]: Cannot call SendText() after SendFile()");

    body = view;
    headers.SetHeader("Content-Length", UInt64ToStr(view.size()));
    headers.SetHeader("Content-Type", CONTENT_TYPE_PLAIN);
}

void HttpResponse::SendText(std::string&& str)
{
    SetTextBody(std::move(str), CONTENT_TYPE_PLAIN);
}

// vvv JSON vvv
void HttpResponse::SendJson(const Json& j)
{
    SetTextBody(std::move(j.dump()), CONTENT_TYPE_JSON);
}

void HttpResponse::SendJson(Json&& j)
{
    SetTextBody(std::move(j).dump(), CONTENT_TYPE_JSON);
}

// vvv FILE vvv
void HttpResponse::SendFile(const char* cstr)
{
    SendFile(std::string_view{cstr});
}

void HttpResponse::SendFile(std::string_view path)
{
    ValidateFileSend(path);
    body = path;
    PrepareFileHeaders(path);
}

void HttpResponse::SendFile(std::string&& path)
{
    ValidateFileSend(path);
    body = std::move(path);
    PrepareFileHeaders(std::get<std::string>(body));
}

// vvv HELPER FUNCTIONS vvv
void HttpResponse::SetTextBody(std::string&& text, const char* contentType)
{
    auto& logger = Logger::GetInstance();

    if(!std::holds_alternative<std::monostate>(body))
        logger.Fatal("[HttpResponse]: Text body already set");

    if(isFileOperation_)
        logger.Fatal("[HttpResponse]: Cannot mix text and file responses");

    body = std::move(text);
    headers.SetHeader("Content-Length", UInt64ToStr(std::get<std::string>(body).size()));
    headers.SetHeader("Content-Type", contentType);
}

void HttpResponse::ValidateFileSend(std::string_view path)
{
    auto& logger = Logger::GetInstance();
    auto& fs     = FileSystem::GetFileSystem();

    if(!std::holds_alternative<std::monostate>(body))
        logger.Fatal("[HttpResponse]: SendFile() called after body already set");

    if(!fs.FileExists(path))
        logger.Fatal("[HttpResponse]: File not found: ", path);
}

void HttpResponse::PrepareFileHeaders(std::string_view path)
{
    auto& fs = FileSystem::GetFileSystem();

    isFileOperation_ = true;

    std::uint64_t    fileSize = fs.GetFileSize(path);
    std::string_view mime     = MimeDetector::DetectMimeFromExt(path);

    headers.SetHeader("Content-Length", UInt64ToStr(fileSize));
    headers.SetHeader("Content-Type", std::string(mime));
}

} // namespace WFX::Http