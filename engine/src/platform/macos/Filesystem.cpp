#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <mach-o/dyld.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Platform.h"
#include "TitleInfo.h"
#include "core/EngineDebug.h"
#include "core/EngineIO.h"

namespace
{

    const char* const kBundleExecutableDirectory = "/Contents/MacOS/";
    const char* const kBundleResourcesDirectory = "/Contents/Resources/";

    bool EnsureDirectory(const char* path)
    {
        struct stat info;
        if (stat(path, &info) == 0)
            return S_ISDIR(info.st_mode);
        return mkdir(path, 0755) == 0;
    }

    bool HomeDirectory(char* out, size_t size)
    {
        const char* home = getenv("HOME");
        if (!home || home[0] == '\0')
        {
            const struct passwd* entry = getpwuid(getuid());
            home = entry ? entry->pw_dir : nullptr;
        }
        if (!home || home[0] == '\0')
            return false;
        return snprintf(out, size, "%s", home) < static_cast<int>(size);
    }

} // namespace

bool MacosPlatform::ResolveDataRoot()
{
    char raw[PATH_MAX];
    uint32_t rawSize = sizeof(raw);
    if (_NSGetExecutablePath(raw, &rawSize) != 0)
    {
        Engine_LogError("%s: the executable path does not fit in %zu bytes", GetName(), sizeof(raw));
        return false;
    }

    char resolved[PATH_MAX];
    if (!realpath(raw, resolved))
    {
        Engine_LogError("%s: could not resolve the executable path '%s'", GetName(), raw);
        return false;
    }

    char* split = strrchr(resolved, '/');
    if (!split)
    {
        Engine_LogError("%s: could not derive a data root from '%s'", GetName(), resolved);
        return false;
    }
    *(split + 1) = '\0';

    const size_t length = strlen(resolved);
    const size_t suffixLength = strlen(kBundleExecutableDirectory);
    if (length >= suffixLength && strcmp(resolved + length - suffixLength, kBundleExecutableDirectory) == 0)
    {
        resolved[length - suffixLength] = '\0';
        if (strlen(resolved) + strlen(kBundleResourcesDirectory) >= sizeof(resolved))
        {
            Engine_LogError("%s: the bundle resources path does not fit", GetName());
            return false;
        }
        strcat(resolved, kBundleResourcesDirectory);
    }

    if (snprintf(m_dataRoot, sizeof(m_dataRoot), "%s", resolved) >= static_cast<int>(sizeof(m_dataRoot)))
    {
        Engine_LogError("%s: the data root '%s' does not fit in %zu bytes", GetName(), resolved, sizeof(m_dataRoot));
        return false;
    }
    return true;
}

bool MacosPlatform::BuildPath(const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!relativePath || !outBuf || bufSize == 0)
        return false;

    while (*relativePath == '/' || *relativePath == '\\')
        ++relativePath;

    const int written = snprintf(outBuf, bufSize, "%s%s", m_dataRoot, relativePath);
    if (written < 0 || static_cast<size_t>(written) >= bufSize)
        return false;

    for (int i = 0; i < written; ++i)
    {
        if (outBuf[i] == '\\')
            outBuf[i] = '/';
    }
    return true;
}

bool MacosPlatform::BuildWritablePath(const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!relativePath || !outBuf || bufSize == 0)
        return false;

    while (*relativePath == '/' || *relativePath == '\\')
        ++relativePath;

    char home[PATH_MAX];
    if (!HomeDirectory(home, sizeof(home)))
    {
        Engine_LogError("%s: no home directory for the save location", GetName());
        return false;
    }

    char root[IO_FILE_MAX_PATH];
    if (snprintf(root, sizeof(root), "%s/Library/Application Support/%s", home, TITLE_DEVELOPER) >= static_cast<int>(sizeof(root)) || !EnsureDirectory(root))
        return false;
    if (snprintf(root, sizeof(root), "%s/Library/Application Support/%s/%s", home, TITLE_DEVELOPER, TITLE_NAME) >= static_cast<int>(sizeof(root)) || !EnsureDirectory(root))
        return false;

    const int written = snprintf(outBuf, bufSize, "%s/%s", root, relativePath);
    if (written < 0 || static_cast<size_t>(written) >= bufSize)
        return false;

    for (int i = 0; i < written; ++i)
    {
        if (outBuf[i] == '\\')
            outBuf[i] = '/';
    }
    return true;
}

FileHandle MacosPlatform::FileOpen(const char* path, FileMode mode)
{
    if (!path)
        return nullptr;
    FILE* file = fopen(path, mode == FileMode::Write ? "wb" : "rb");
    return reinterpret_cast<FileHandle>(file);
}

bool MacosPlatform::FileSeek(FileHandle file, uint64_t offset)
{
    if (!file)
        return false;
    return fseeko(reinterpret_cast<FILE*>(file), static_cast<off_t>(offset), SEEK_SET) == 0;
}

size_t MacosPlatform::FileWrite(FileHandle file, const void* src, size_t bytes)
{
    if (!file || !src)
        return 0;
    return fwrite(src, 1, bytes, reinterpret_cast<FILE*>(file));
}

size_t MacosPlatform::FileRead(FileHandle file, void* dst, size_t bytes)
{
    if (!file || !dst)
        return 0;
    return fread(dst, 1, bytes, reinterpret_cast<FILE*>(file));
}

uint64_t MacosPlatform::FileSize(FileHandle file) const
{
    if (!file)
        return 0;

    FILE* f = reinterpret_cast<FILE*>(file);
    struct stat info;
    if (fstat(fileno(f), &info) != 0 || info.st_size < 0)
        return 0;
    return static_cast<uint64_t>(info.st_size);
}

void MacosPlatform::FileClose(FileHandle file)
{
    if (file)
        fclose(reinterpret_cast<FILE*>(file));
}
