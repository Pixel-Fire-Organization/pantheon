#include <cstdio>
#include <cstring>

#include "core/EngineDebug.h"
#include "core/EngineIO.h"
#include "Platform.h"
#include "TitleInfo.h"

#include <shlobj.h>
#include <windows.h>

// ---------------------------------------------------------------------------
// Win32 storage.
//
// There is no device token here: assets live in a directory next to the
// executable, which is what makes dist/win32/ runnable by copying the folder
// alone. Paths are resolved against that root rather than the process working
// directory, so launching from anywhere behaves the same.
// ---------------------------------------------------------------------------

bool Win32Platform::ResolveDataRoot()
{
    char exePath[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        Engine_LogError("%s: GetModuleFileName failed (%lu)", GetName(), GetLastError());
        return false;
    }

    // Trim the executable name, keeping the trailing separator.
    char* lastSlash = strrchr(exePath, '\\');
    char* lastFwd = strrchr(exePath, '/');
    char* split = (lastSlash > lastFwd) ? lastSlash : lastFwd;
    if (!split)
    {
        Engine_LogError("%s: could not derive a data root from '%s'", GetName(), exePath);
        return false;
    }
    *(split + 1) = '\0';

    strncpy(m_dataRoot, exePath, sizeof(m_dataRoot) - 1);
    m_dataRoot[sizeof(m_dataRoot) - 1] = '\0';

    // Normalise to forward slashes: the baked asset keys use them, and the Win32
    // file APIs accept either.
    for (char* p = m_dataRoot; *p; ++p)
    {
        if (*p == '\\')
            *p = '/';
    }
    return true;
}

bool Win32Platform::BuildPath(const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!relativePath || !outBuf || bufSize == 0)
        return false;

    // Skip a leading separator so the join never doubles up.
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

bool Win32Platform::BuildWritablePath(const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!relativePath || !outBuf || bufSize == 0)
        return false;

    while (*relativePath == '/' || *relativePath == '\\')
        ++relativePath;

    char local[MAX_PATH];
    if (!SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local)))
    {
        Engine_LogError("%s: no local application data directory", GetName());
        return false;
    }

    char root[IO_FILE_MAX_PATH];
    if (snprintf(root, sizeof(root), "%s/%s", local, TITLE_DEVELOPER) >= static_cast<int>(sizeof(root)))
        return false;
    CreateDirectoryA(root, nullptr);

    if (snprintf(root, sizeof(root), "%s/%s/%s", local, TITLE_DEVELOPER, TITLE_NAME) >= static_cast<int>(sizeof(root)))
        return false;
    CreateDirectoryA(root, nullptr);

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

FileHandle Win32Platform::FileOpen(const char* path, FileMode mode)
{
    if (!path)
        return nullptr;
    FILE* file = fopen(path, mode == FileMode::Write ? "wb" : "rb");
    return reinterpret_cast<FileHandle>(file);
}

bool Win32Platform::FileSeek(FileHandle file, uint64_t offset)
{
    if (!file)
        return false;
    // _fseeki64: assets can exceed 2 GB on a desktop target, where the PS2 long
    // offset would silently truncate.
    return _fseeki64(reinterpret_cast<FILE*>(file), static_cast<long long>(offset), SEEK_SET) == 0;
}

size_t Win32Platform::FileWrite(FileHandle file, const void* src, size_t bytes)
{
    if (!file || !src)
        return 0;
    return fwrite(src, 1, bytes, reinterpret_cast<FILE*>(file));
}

size_t Win32Platform::FileRead(FileHandle file, void* dst, size_t bytes)
{
    if (!file || !dst)
        return 0;
    return fread(dst, 1, bytes, reinterpret_cast<FILE*>(file));
}

uint64_t Win32Platform::FileSize(FileHandle file) const
{
    if (!file)
        return 0;

    FILE* f = reinterpret_cast<FILE*>(file);
    const long long current = _ftelli64(f);
    if (current < 0 || _fseeki64(f, 0, SEEK_END) != 0)
        return 0;

    const long long size = _ftelli64(f);
    _fseeki64(f, current, SEEK_SET);
    return (size < 0) ? 0 : static_cast<uint64_t>(size);
}

void Win32Platform::FileClose(FileHandle file)
{
    if (file)
        fclose(reinterpret_cast<FILE*>(file));
}
