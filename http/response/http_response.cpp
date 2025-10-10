#include "http_response.hpp"

#include "engine/template_engine.hpp"
#include "http/common/http_detector.hpp"
#include "utils/filesystem/filesystem.hpp"
#include "utils/backport/string.hpp"
#include "utils/logger/logger.hpp"

#include <nlohmann/json.hpp>

#include <type_traits>
#include <charconv>

namespace WFX::Http {

using namespace WFX::Utils; // For 'Logger', 'FileSystem'
using namespace WFX::Core;  // For 'TemplateEngine'

constexpr const char* CONTENT_TYPE_PLAIN = "text/plain";
constexpr const char* CONTENT_TYPE_JSON  = "application/json";

HttpResponse& HttpResponse::Status(HttpStatus code)
{
    status = code;
    return *this;
}

HttpResponse& HttpResponse::Set(std::string&& key, std::string&& value)
{
    headers.SetHeader(std::move(key), std::move(value));
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
    auto& logger = Logger::GetInstance();
    
    if(!std::holds_alternative<std::monostate>(body))
        logger.Fatal("[HttpResponse]: SendText() called after response body already set");
    
    if(isFileOperation_)
        logger.Fatal("[HttpResponse]: Cannot call SendText() after SendFile()");

    auto view = std::string_view{cstr};

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
void HttpResponse::SendFile(const char* cstr, bool autoHandle404)
{
    auto view = std::string_view{cstr};

    if(!ValidateFileSend(view, autoHandle404))
        return;

    body = view;
    PrepareFileHeaders(view);
}

void HttpResponse::SendFile(std::string&& path, bool autoHandle404)
{
    if(!ValidateFileSend(path, autoHandle404))
        return;

    body = std::move(path);
    PrepareFileHeaders(std::get<std::string>(body));
}

void HttpResponse::SendTemplate(const char* cstr, bool autoHandle404)
{
    SendTemplate(std::string(cstr), autoHandle404);
}

void HttpResponse::SendTemplate(std::string&& path, bool autoHandle404)
{
    if(!std::holds_alternative<std::monostate>(body))
        Logger::GetInstance()
            .Fatal("[HttpResponse]: SendTemplate() called after body already set");

    auto meta = TemplateEngine::GetInstance().GetTemplate(std::move(path));
    if(!meta && autoHandle404) {
        Status(HttpStatus::NOT_FOUND)
            .SendText("Template not found");
        return;
    }

    isFileOperation_ = true;

    // If template meta exists, template file exists as well
    // Rn we just handle 'static' templates which can be served as is
    body = std::string_view{meta->fullPath};

    // Set remaining
    headers.SetHeader("Content-Length", UInt64ToStr(meta->size));
    headers.SetHeader("Content-Type", "text/html");
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

bool HttpResponse::ValidateFileSend(std::string_view path, bool autoHandle404)
{
    auto& logger = Logger::GetInstance();
    auto& fs     = FileSystem::GetFileSystem();

    if(!std::holds_alternative<std::monostate>(body))
        logger.Fatal("[HttpResponse]: SendFile() called after body already set");

    if(autoHandle404 && !fs.FileExists(path.data())) {
        Status(HttpStatus::NOT_FOUND)
            .SendText("File not found");
        return false;
    }

    return true;
}

void HttpResponse::PrepareFileHeaders(std::string_view path)
{
    auto& fs = FileSystem::GetFileSystem();

    isFileOperation_ = true;

    std::uint64_t    fileSize = fs.GetFileSize(path.data());
    std::string_view mime     = MimeDetector::DetectMimeFromExt(path);

    headers.SetHeader("Content-Length", UInt64ToStr(fileSize));
    headers.SetHeader("Content-Type", std::string(mime));
}

} // namespace WFX::Http