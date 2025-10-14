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
using BufferPtr      = std::unique_ptr<char[]>;

class TemplateEngine final {
public:
    static TemplateEngine& GetInstance();

    bool          LoadTemplatesFromCache(); // ---
    void          SaveTemplatesToCache();   //   | -> To be called in master process only
    void          PreCompileTemplates();    // ---
    TemplateMeta* GetTemplate(std::string&& relPath);

private: // Nested helper types for the parser
    enum class LineResult {
        FAILURE,
        PROCESSED_INCLUDE,
        REGULAR_LINE
    };

    // Generic buffered I/O for writing
    struct IOContext {
        BaseFilePtr   file;
        BufferPtr     buffer;
        std::uint32_t chunkSize{0};
        std::uint32_t offset{0};

        IOContext(BaseFilePtr f, std::uint32_t chunk)
            : file(std::move(f)),
            buffer(std::make_unique<char[]>(chunk)),
            chunkSize(chunk)
        {}
    };

    struct TemplateFrame {
        BaseFilePtr   file;
        BufferPtr     readBuf;
        std::string   carry;
        bool          firstRead{true};

        TemplateFrame(BaseFilePtr f, std::uint32_t chunkSize)
            : file(std::move(f)),
            readBuf(std::make_unique<char[]>(chunkSize))
        {}
    };
    
    // Compilation context
    struct CompilationContext {
        IOContext                 io;          // Unified write buffer
        std::deque<TemplateFrame> stack;       // Recursive includes
        std::size_t               chunkSize{0};
        bool                      foundInclude{false};

        CompilationContext(BaseFilePtr out, std::uint32_t chunk)
            : io(std::move(out), chunk),
            chunkSize(chunk)
        {}
    };

private: // Helper functions
    TemplateResult CompileTemplate(BaseFilePtr inTemplate, BaseFilePtr outTemplate);
    bool           PushInclude(CompilationContext& context, const std::string& relPath);
    LineResult     ProcessLine(CompilationContext& context, const std::string& line);

private: // IO Functions
    bool FlushWrite(IOContext& context, bool force = false);
    bool SafeWrite(IOContext& context, const void* data, std::size_t size);

private:
    TemplateEngine()  = default;
    ~TemplateEngine() = default;

    // Don't need any copy / move semantics
    TemplateEngine(const TemplateEngine&)            = delete;
    TemplateEngine(TemplateEngine&&)                 = delete;
    TemplateEngine& operator=(const TemplateEngine&) = delete;
    TemplateEngine& operator=(TemplateEngine&&)      = delete;

private: // For ease of use across functions
    constexpr static std::string_view partialTag_     = "{% partial %}";
    constexpr static std::size_t      partialTagSize_ = partialTag_.size();

    constexpr static const char*      cacheFile_      = "/build/templates/cache.bin";
    constexpr static const char*      staticFolder_   = "/build/templates/static";

private: // Storage
    Logger& logger_ = Logger::GetInstance();

    // We don't want to save template data to cache.bin always, only save it if we-
    // -compile the templates, in which case there might be a chance the data is modified
    bool resaveCacheFile_ = false;

    // CRITICAL WARNING: The data in this map MUST be treated as immutable after initial-
    // -population. Internal engine code may store string_views that point directly to the-
    // -'fullPath' strings contained here. Modifying this map at runtime (e.g., adding,-
    // -removing, or reloading templates) will cause dangling pointers and crash the server
    std::unordered_map<std::string, TemplateMeta> templates_;
};

} // namespace WFX::Core

#endif // WFX_TEMPLATE_ENGINE_HPP