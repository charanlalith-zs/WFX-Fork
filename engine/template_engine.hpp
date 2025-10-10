#ifndef WFX_TEMPLATE_ENGINE_HPP
#define WFX_TEMPLATE_ENGINE_HPP

#include "utils/logger/logger.hpp"
#include "utils/filesystem/filesystem.hpp"
#include <string>
#include <unordered_map>
#include <deque>
#include <cstdint>

namespace WFX::Core {

using namespace WFX::Utils; // For 'Logger', ...

enum class TemplateType : std::uint8_t {
    FAILURE,         // Failed to compile
    PURE_STATIC,     // No template semantics, serve from build/templates/static/
    COMPILED_STATIC  // Precompiled includes,  serve from build/templates/static/
};

struct TemplateMeta {
    TemplateType type;
    std::size_t  size;
    std::string  fullPath; // Full path to the file to serve
};

// <Type, FileSize>
using TemplateResult = std::pair<TemplateType, std::size_t>;

class TemplateEngine final {
public:
    static TemplateEngine& GetInstance();

    void          PreCompileTemplates();
    TemplateMeta* GetTemplate(std::string&& relPath);

private: // Nested helper types for the parser
    enum class LineResult {
        FAILURE,
        PROCESSED_INCLUDE,
        REGULAR_LINE
    };

    struct TemplateFrame {
        BaseFilePtr             file;
        std::unique_ptr<char[]> readBuf;
        std::unique_ptr<char[]> writeBuf;
        std::size_t             writePos{0};
        std::string             carry;
        bool                    firstRead{true};

        TemplateFrame(BaseFilePtr f, std::size_t chunkSize) :
            file(std::move(f)),
            readBuf(std::make_unique<char[]>(chunkSize)),
            writeBuf(std::make_unique<char[]>(chunkSize))
        {}
    };
    
    struct CompilationContext {
        BaseFilePtr               outTemplate;
        std::deque<TemplateFrame> stack;
        bool                      foundInclude;
        std::size_t               chunkSize;
    };

private: // Helper functions
    TemplateResult CompileTemplate(BaseFilePtr inTemplate, BaseFilePtr outTemplate);

private: // Even more helper functions
    bool       FlushWrite(CompilationContext& context, TemplateFrame& frame, bool force = false);
    bool       SafeWrite(CompilationContext& context, TemplateFrame& frame, const char* data, std::size_t size);
    bool       PushInclude(CompilationContext& context, const std::string& relPath);
    LineResult ProcessLine(CompilationContext& context, const std::string& line);

private:
    TemplateEngine()  = default;
    ~TemplateEngine() = default;

    // Don't need any copy / move semantics
    TemplateEngine(const TemplateEngine&)            = delete;
    TemplateEngine(TemplateEngine&&)                 = delete;
    TemplateEngine& operator=(const TemplateEngine&) = delete;
    TemplateEngine& operator=(TemplateEngine&&)      = delete;

private: // For ease of use across functions
    constexpr static std::string_view partialTag     = "{% partial %}";
    constexpr static std::size_t      partialTagSize = partialTag.size();

private: // Storage
    Logger& logger_ = Logger::GetInstance();

    // CRITICAL WARNING: The data in this map MUST be treated as immutable after initial
    // population. Internal engine code may store string_views that point directly to the
    // 'fullPath' strings contained here. Modifying this map at runtime (e.g., adding,
    // removing, or reloading templates) will cause dangling pointers and crash the server
    std::unordered_map<std::string, TemplateMeta> templates_;
};

} // namespace WFX::Core

#endif // WFX_TEMPLATE_ENGINE_HPP