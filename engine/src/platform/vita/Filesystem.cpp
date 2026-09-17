#include <cstdio>
#include <cstring>

#include "Platform.h"
#include "core/EngineDebug.h"
#include "core/EngineIO.h"

extern "C" {
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
}

namespace
{
    FileHandle ToHandle(SceUID fd) { return reinterpret_cast<FileHandle>(static_cast<uintptr_t>(fd) + 1u); }

    SceUID ToFd(FileHandle handle) { return static_cast<SceUID>(reinterpret_cast<uintptr_t>(handle) - 1u); }
} // namespace

bool VitaPlatform::BuildPath(const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!relativePath || !outBuf || bufSize == 0)
        return false;

    while (*relativePath == '/' || *relativePath == '\\')
        ++relativePath;

    const size_t tokenLength = strlen(m_resourceToken);
    const char* separator = (tokenLength && m_resourceToken[tokenLength - 1] == ':') ? "" : "/";
    const int written = snprintf(outBuf, bufSize, "%s%s%s", m_resourceToken, separator, relativePath);
    if (written < 0 || static_cast<size_t>(written) >= bufSize)
        return false;

    for (int i = 0; i < written; ++i)
    {
        if (outBuf[i] == '\\')
            outBuf[i] = '/';
    }
    return true;
}

bool VitaPlatform::BuildWritablePath(const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!relativePath || !outBuf || bufSize == 0 || !m_writableRoot[0])
        return false;

    while (*relativePath == '/' || *relativePath == '\\')
        ++relativePath;

    sceIoMkdir(m_writableRoot, 0777);

    const int written = snprintf(outBuf, bufSize, "%s%s", m_writableRoot, relativePath);
    if (written < 0 || static_cast<size_t>(written) >= bufSize)
        return false;

    for (int i = 0; i < written; ++i)
    {
        if (outBuf[i] == '\\')
            outBuf[i] = '/';
    }
    return true;
}

FileHandle VitaPlatform::FileOpen(const char* path, FileMode mode)
{
    if (!path)
        return nullptr;

    SceUID fd;
    if (mode == FileMode::Write)
    {
        fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    }
    else
    {
        fd = sceIoOpen(path, SCE_O_RDONLY, 0777);
    }

    if (fd < 0)
        return nullptr;
    return ToHandle(fd);
}

bool VitaPlatform::FileSeek(FileHandle file, uint64_t offset)
{
    if (!file)
        return false;
    return sceIoLseek(ToFd(file), static_cast<SceOff>(offset), SCE_SEEK_SET) >= 0;
}

size_t VitaPlatform::FileRead(FileHandle file, void* dst, size_t bytes)
{
    if (!file || !dst)
        return 0;
    const SceSSize read = sceIoRead(ToFd(file), dst, static_cast<SceSize>(bytes));
    return (read < 0) ? 0u : static_cast<size_t>(read);
}

size_t VitaPlatform::FileWrite(FileHandle file, const void* src, size_t bytes)
{
    if (!file || !src)
        return 0;
    const SceSSize written = sceIoWrite(ToFd(file), src, static_cast<SceSize>(bytes));
    return (written < 0) ? 0u : static_cast<size_t>(written);
}

uint64_t VitaPlatform::FileSize(FileHandle file) const
{
    if (!file)
        return 0;

    const SceUID fd = ToFd(file);
    const SceOff current = sceIoLseek(fd, 0, SCE_SEEK_CUR);
    if (current < 0)
        return 0;

    const SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, current, SCE_SEEK_SET);
    return (size < 0) ? 0u : static_cast<uint64_t>(size);
}

void VitaPlatform::FileClose(FileHandle file)
{
    if (file)
        sceIoClose(ToFd(file));
}
