#include "graphics/ShaderAssets.h"

#include <cstdio>
#include <cstring>
#include <utility>

#include "core/CommandLine.h"
#include "core/EngineDebug.h"
#include "platform/Platform.h"

namespace
{

    const char* const kOverrideOption = "shaders";
    const size_t kPathCapacity = 1024;
    const size_t kRelativeCapacity = 256;

    bool ResolvePath(Platform* platform, const char* name, char* out, size_t capacity)
    {
        const StartupArgs& args = platform->GetStartupArgs();
        const char* directory = args.commandLine ? args.commandLine->GetString(kOverrideOption, nullptr) : nullptr;
        if (directory && directory[0] != '\0')
        {
            const int written = snprintf(out, capacity, "%s/%s", directory, name);
            return written > 0 && static_cast<size_t>(written) < capacity;
        }

        char relative[kRelativeCapacity];
        const int length = snprintf(relative, sizeof(relative), "%s/%s", SHADER_ASSET_DIRECTORY, name);
        if (length <= 0 || static_cast<size_t>(length) >= sizeof(relative))
            return false;
        return platform->BuildPath(relative, out, capacity);
    }

} // namespace

bool ShaderAssets_Load(const char* name, ShaderSource* out)
{
    if (!name || !out)
        return false;
    out->text.reset();
    out->size = 0;

    Platform* platform = Engine_GetPlatform();
    char path[kPathCapacity];
    if (!ResolvePath(platform, name, path, sizeof(path)))
    {
        Engine_LogError("ShaderAssets: the path for '%s' does not fit in %zu bytes", name, sizeof(path));
        return false;
    }

    FileHandle file = platform->FileOpen(path, FileMode::Read);
    if (!file)
    {
        Engine_LogError("ShaderAssets: cannot open '%s'. Run the shader cook (cook-%s) or pass --shaders <dir>.", path, platform->GetName());
        return false;
    }

    const uint64_t size = platform->FileSize(file);
    if (size == 0 || size > static_cast<uint64_t>(SHADER_SOURCE_MAX_BYTES))
    {
        Engine_LogError("ShaderAssets: '%s' is %llu bytes; a shader must be 1 to %u bytes", path, static_cast<unsigned long long>(size), static_cast<unsigned>(SHADER_SOURCE_MAX_BYTES));
        platform->FileClose(file);
        return false;
    }

    const size_t length = static_cast<size_t>(size);
    PlatformArray<char> text = Engine_PlatformArray<char>(length + 1u);
    if (!text)
    {
        Engine_LogError("ShaderAssets: no memory for '%s' (%zu bytes)", path, length + 1u);
        platform->FileClose(file);
        return false;
    }

    const size_t got = platform->FileRead(file, text.get(), length);
    platform->FileClose(file);
    if (got != length)
    {
        Engine_LogError("ShaderAssets: '%s' read %zu of %zu bytes", path, got, length);
        return false;
    }

    text[length] = '\0';
    if (memchr(text.get(), '\0', length))
    {
        Engine_LogError("ShaderAssets: '%s' contains a NUL byte, which would end the source early", path);
        return false;
    }

    out->text = std::move(text);
    out->size = static_cast<uint32_t>(length);
    return true;
}
