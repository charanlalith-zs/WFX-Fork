#include "template_engine.hpp"

#include "config/config.hpp"
#include "utils/backport/string.hpp"
#include <cstring>
#include <deque>

namespace WFX::Core {

TemplateEngine& TemplateEngine::GetInstance()
{
    static TemplateEngine engine;
    return engine;
}

void TemplateEngine::PreCompileTemplates()
{
    auto& fs     = FileSystem::GetFileSystem();
    auto& config = Config::GetInstance();

    std::size_t        errors    = 0;
    const std::string& inputDir  = config.projectConfig.templateDir;
    const std::string  outputDir = config.projectConfig.projectName + "/build/templates/static";

    // For partial tag checking. If u see it, don't compile the .html file
    // + 1 is redundant btw, but im still keeping it to cover my paranoia
    char temp[partialTagSize + 1] = { 0 };

    if(!fs.DirectoryExists(outputDir.c_str()) && !fs.CreateDirectory(outputDir, true))
        logger_.Fatal("[TemplateEngine]: Failed to create base template directory: ", outputDir);
    
    logger_.Info("[TemplateEngine]: Starting template precompilation from: ", inputDir);

    // Traverse the entire template directory looking for .html files
    // Then compile those html files into /build/templates/(static | dynamic)/
    fs.ListDirectory(inputDir, true, [&](std::string inPath) {
        if(!EndsWith(inPath, ".html"))
            return;
        
        logger_.Info("[TemplateEngine]: Compiling template: ", inPath);

        // Strip leading slash cuz we will use this as key inside of templates_ map
        // We expect that user inside of SendTemplate function inputs path without leading slash
        // And because SendTemplate uses templates_ map, we cannot use leading slash
        std::string relPath = std::string(inPath.begin() + inputDir.size(), inPath.end());
        relPath.erase(0, relPath.find_first_not_of("/\\"));

        const std::string outPath = outputDir + "/" + relPath;

        // Ensure target directory exists
        std::string relOutputDir = outPath.substr(0, outPath.find_last_of("/\\"));
        if(!fs.DirectoryExists(relOutputDir.c_str()) && !fs.CreateDirectory(relOutputDir, true)) {
            logger_.Error("[TemplateEngine]: Failed to create template output directory: ", outputDir);
            return;
        }

        auto in = fs.OpenFileRead(inPath.c_str());
        if(!in) {
            errors++;
            logger_.Error("[TemplateEngine]: Failed to open input template file: ", inPath);
            return;
        }
        auto inSize = in->Size();

        // File empty
        if(inSize == 0)
            return;

        // Partial tag can exist, check its existence first
        if(inSize >= partialTagSize) {        
            if(in->Read(temp, partialTagSize) < 0) {
                errors++;
                logger_.Error("[TemplateEngine]: Failed to read the first 14 bytes");
                return;
            }

            // No need to compile
            if(StartsWith(temp, partialTag))
                return;

            // Seek back to start to not miss those bytes we skipped
            if(!in->Seek(0)) {
                errors++;
                logger_.Error("[TemplateEngine]: Failed to revert back to the start, lost 14 bytes");
                return;
            }
        }

        auto out = fs.OpenFileWrite(outPath.c_str());
        if(!out) {
            errors++;
            logger_.Error("[TemplateEngine]: Failed to open output template file: ", outPath);
            return;
        }

        auto [type, outSize] = CompileTemplate(std::move(in), std::move(out));
        if(type == TemplateType::FAILURE)
            errors++;
        
        // Add it to our template map
        else
            templates_.emplace(
                std::move(relPath), TemplateMeta{type, outSize, std::move(outPath)}
            );
    });

    if(errors > 0)
        logger_.Warn("[TemplateEngine]: Template compilation complete with ", errors, " error(s)");
    else
        logger_.Info("[TemplateEngine]: Template compilation completed successfully");
}

TemplateMeta* TemplateEngine::GetTemplate(std::string&& relPath)
{
    auto templateMeta = templates_.find(std::move(relPath));
    if(templateMeta != templates_.end())
        return &(templateMeta->second);

    return nullptr;
}

// vvv Helper Functions vvv
TemplateResult TemplateEngine::CompileTemplate(BaseFilePtr inTemplate, BaseFilePtr outTemplate)
{
    std::size_t        chunkSize = Config::GetInstance().miscConfig.templateChunkSize;
    auto&              fs        = FileSystem::GetFileSystem();
    CompilationContext ctx       = {std::move(outTemplate), {}, false, chunkSize};
    
    // Initialize stack with the current template
    auto& stack = ctx.stack;
    stack.emplace_back(std::move(inTemplate), ctx.chunkSize);

    while(!stack.empty()) {
        TemplateFrame& frame = stack.back();

        std::int64_t bytesRead = frame.file->Read(frame.readBuf.get(), ctx.chunkSize);
        if(bytesRead < 0)
            return { TemplateType::FAILURE, 0 };

        if(bytesRead == 0) {
            if(!frame.carry.empty() && !SafeWrite(ctx, frame, frame.carry.data(), frame.carry.size()))
                return { TemplateType::FAILURE, 0 };

            if(!FlushWrite(ctx, frame, true))
                return { TemplateType::FAILURE, 0 };

            stack.pop_back();
            continue;
        }

        std::size_t pos = 0;
        char* bufPtr = frame.readBuf.get();

        if(frame.firstRead) {
            frame.firstRead = false;
            if(
                bytesRead >= partialTagSize
                && StartsWith(std::string_view(bufPtr, partialTagSize), partialTag)
            )
                pos += partialTagSize + 1;
        }

        while(pos < static_cast<std::size_t>(bytesRead)) {
            std::string line;
            bool hasNewline = false;
            if(!frame.carry.empty()) {
                line = std::move(frame.carry);
                frame.carry.clear();
            }

            std::size_t lineStartPos = pos;
            for(; pos < static_cast<std::size_t>(bytesRead); ++pos) {
                if(bufPtr[pos] == '\n') {
                    hasNewline = true;
                    line.append(bufPtr + lineStartPos, pos - lineStartPos + 1);
                    ++pos;
                    break;
                }
            }

            if(!hasNewline)
                line.append(bufPtr + lineStartPos, pos - lineStartPos);
            
            if(hasNewline) {
                switch(ProcessLine(ctx, line)) {
                    case LineResult::FAILURE:
                        return { TemplateType::FAILURE, 0 };
                    case LineResult::REGULAR_LINE:
                        if(!SafeWrite(ctx, frame, line.data(), line.size()))
                            return { TemplateType::FAILURE, 0 };
                        break;
                    case LineResult::PROCESSED_INCLUDE:
                        break; // Discard line
                }
            }
            else
                frame.carry = std::move(line);
        }
    }
    return {
        ctx.foundInclude ? TemplateType::COMPILED_STATIC : TemplateType::PURE_STATIC,
        ctx.outTemplate->Size()
    };
}

// vvv AAAAAAAAAAAAAAAAAAAAAAAAA vvv
bool TemplateEngine::FlushWrite(CompilationContext& context, TemplateFrame& frame, bool force)
{
    if(frame.writePos == 0)
        return true;
    
    if(force || frame.writePos >= context.chunkSize) {
        if(context.outTemplate->Write(frame.writeBuf.get(), frame.writePos)
            != static_cast<std::int64_t>(frame.writePos))
        {
            logger_.Error("[TemplateEngine]: Failed to write chunk");
            return false;
        }
        frame.writePos = 0;
    }
    return true;
}

bool TemplateEngine::SafeWrite(CompilationContext& context, TemplateFrame& frame,
                                const char* data, std::size_t size)
{
    while(size > 0) {
        std::size_t available = context.chunkSize - frame.writePos;
        std::size_t toCopy = std::min(size, available);
        std::memcpy(frame.writeBuf.get() + frame.writePos, data, toCopy);
        
        frame.writePos += toCopy;
        data += toCopy;
        size -= toCopy;

        if(frame.writePos >= context.chunkSize && !FlushWrite(context, frame, true))
            return false;
    }
    return true;
}

bool TemplateEngine::PushInclude(CompilationContext& context, const std::string& relPath)
{
    auto& fs     = FileSystem::GetFileSystem();
    auto& config = Config::GetInstance();

    std::string fullPath = config.projectConfig.templateDir + "/" + relPath;

    BaseFilePtr newFile = fs.OpenFileRead(fullPath.c_str());
    if(!newFile) {
        logger_.Error("[TemplateEngine]: Cannot open include '", fullPath, '\'');
        return false;
    }
    context.stack.emplace_back(std::move(newFile), context.chunkSize);
    return true;
}

TemplateEngine::LineResult TemplateEngine::ProcessLine(
    CompilationContext& context, const std::string& line
)
{
    std::size_t tagPos = line.find("{% include");
    if(tagPos == std::string::npos)
        return LineResult::REGULAR_LINE;

    std::size_t tagEnd = line.find("%}", tagPos);
    if(tagEnd == std::string::npos)
        return LineResult::FAILURE;

    std::string tagBody = line.substr(tagPos + 2, tagEnd - (tagPos + 2));
    TrimInline(tagBody);

    std::size_t q1 = tagBody.find_first_of("'\"");
    std::size_t q2 = tagBody.find_last_of("'\"");
    if(q1 == std::string::npos || q2 <= q1)
        return LineResult::FAILURE;

    std::string includePath = tagBody.substr(q1 + 1, q2 - q1 - 1);
    context.foundInclude = true;
    return PushInclude(context, includePath) ? LineResult::PROCESSED_INCLUDE : LineResult::FAILURE;
}

} // namespace WFX::Core