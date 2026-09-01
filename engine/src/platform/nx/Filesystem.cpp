#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/EngineIO.h"
#include "Platform.h"

namespace
{
    const char* const NX_HOMEBREW_DIRECTORY = "sdmc:/switch";

    FileHandle ToHandle(int fd) { return reinterpret_cast<FileHandle>(static_cast<uintptr_t>(fd) + 1u); }

    int ToFd(FileHandle handle) { return static_cast<int>(reinterpret_cast<uintptr_t>(handle) - 1u); }

    bool IsDirectory(const char* path)
    {
        struct stat info;
        return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
    }

    bool MakeDirectory(const char* path)
    {
        if (IsDirectory(path))
            return true;
        return mkdir(path, 0777) == 0 || IsDirectory(path);
    }
}

bool NxPlatform::EnsureWritableRoot() const
{
    if (!m_writableRoot[0])
        return false;

    if (!MakeDirectory(NX_HOMEBREW_DIRECTORY))
        return false;

    char directory[IO_FILE_MAX_PATH];
    const size_t length = strlen(m_writableRoot);
    if (length == 0 || length >= sizeof(directory))
        return false;

    memcpy(directory, m_writableRoot, length + 1u);
    if (directory[length - 1u] == '/')
        directory[length - 1u] = '\0';

    return MakeDirectory(directory);
}

bool NxPlatform::AppendResolved(const char* root, const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!root || !relativePath || !outBuf || bufSize == 0)
        return false;

    while (*relativePath == '/' || *relativePath == '\\')
        ++relativePath;

    const size_t rootLength = strlen(root);
    const char* separator = (rootLength == 0 || root[rootLength - 1] == '/') ? "" : "/";

    const int written = snprintf(outBuf, bufSize, "%s%s%s", root, separator, relativePath);
    if (written < 0 || static_cast<size_t>(written) >= bufSize)
        return false;

    for (int i = 0; i < written; ++i)
    {
        if (outBuf[i] == '\\')
            outBuf[i] = '/';
    }
    return true;
}

bool NxPlatform::BuildPath(const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!m_romfsMounted)
        return false;
    return AppendResolved(m_resourceToken, relativePath, outBuf, bufSize);
}

bool NxPlatform::BuildWritablePath(const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!m_sdMounted || !EnsureWritableRoot())
        return false;
    return AppendResolved(m_writableRoot, relativePath, outBuf, bufSize);
}

FileHandle NxPlatform::FileOpen(const char* path, FileMode mode)
{
    if (!path)
        return nullptr;

    const int fd = (mode == FileMode::Write) ? open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666) : open(path, O_RDONLY);
    if (fd < 0)
        return nullptr;
    return ToHandle(fd);
}

bool NxPlatform::FileSeek(FileHandle file, uint64_t offset)
{
    if (!file)
        return false;
    return lseek(ToFd(file), static_cast<off_t>(offset), SEEK_SET) >= 0;
}

size_t NxPlatform::FileRead(FileHandle file, void* dst, size_t bytes)
{
    if (!file || !dst)
        return 0;
    const ssize_t count = read(ToFd(file), dst, bytes);
    return (count < 0) ? 0u : static_cast<size_t>(count);
}

size_t NxPlatform::FileWrite(FileHandle file, const void* src, size_t bytes)
{
    if (!file || !src)
        return 0;
    const ssize_t count = write(ToFd(file), src, bytes);
    return (count < 0) ? 0u : static_cast<size_t>(count);
}

uint64_t NxPlatform::FileSize(FileHandle file) const
{
    if (!file)
        return 0;

    struct stat info;
    if (fstat(ToFd(file), &info) != 0 || info.st_size < 0)
        return 0;
    return static_cast<uint64_t>(info.st_size);
}

void NxPlatform::FileClose(FileHandle file)
{
    if (file)
        close(ToFd(file));
}
