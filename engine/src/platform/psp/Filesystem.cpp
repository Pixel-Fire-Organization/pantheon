#include <cstdio>
#include <cstring>

#include "Platform.h"
#include "TitleInfo.h"
#include "core/EngineDebug.h"
#include "core/EngineIO.h"

extern "C" {
#include <pspiofilemgr.h>
}

namespace
{
    FileHandle ToHandle(SceUID fd) { return reinterpret_cast<FileHandle>(static_cast<uintptr_t>(fd) + 1u); }

    SceUID ToFd(FileHandle handle) { return static_cast<SceUID>(reinterpret_cast<uintptr_t>(handle) - 1u); }

    bool HasPrefix(const char* text, const char* prefix) { return text && prefix && strncmp(text, prefix, strlen(prefix)) == 0; }

    // Copy everything up to and including the final separator, so a launch path
    // becomes the directory it lives in.
    bool DirectoryOf(const char* path, char* outBuf, size_t bufSize)
    {
        if (!path || !outBuf || bufSize == 0)
            return false;

        const char* lastSlash = strrchr(path, '/');
        if (!lastSlash)
            return false;

        const size_t length = static_cast<size_t>(lastSlash - path) + 1u;
        if (length >= bufSize)
            return false;

        memcpy(outBuf, path, length);
        outBuf[length] = '\0';
        return true;
    }
} // namespace

void PspPlatform::ResolveDeviceToken(const char* launchPath)
{
    m_writableRoot[0] = '\0';

    if (HasPrefix(launchPath, "host"))
    {
        if (DirectoryOf(launchPath, m_resourceRoot, sizeof(m_resourceRoot)))
        {
            m_resourceToken = m_resourceRoot;
            snprintf(m_writableRoot, sizeof(m_writableRoot), "%s", m_resourceRoot);
            return;
        }
    }

    if (HasPrefix(launchPath, "ms0:") || HasPrefix(launchPath, "ef0:"))
    {
        if (DirectoryOf(launchPath, m_resourceRoot, sizeof(m_resourceRoot)))
        {
            m_resourceToken = m_resourceRoot;
            snprintf(m_writableRoot, sizeof(m_writableRoot), "%s", m_resourceRoot);
            return;
        }
    }

    if (HasPrefix(launchPath, "disc0:"))
    {
        snprintf(m_resourceRoot, sizeof(m_resourceRoot), "disc0:/PSP_GAME/USRDIR/");
        m_resourceToken = m_resourceRoot;
        snprintf(m_writableRoot, sizeof(m_writableRoot), "ms0:/PSP/GAME/%s/", TITLE_ID_PSP);
        if (sceIoMkdir(m_writableRoot, 0777) < 0)
        {
            SceIoStat stat;
            memset(&stat, 0, sizeof(stat));
            if (sceIoGetstat(m_writableRoot, &stat) < 0)
                m_writableRoot[0] = '\0';
        }
        return;
    }

    snprintf(m_resourceRoot, sizeof(m_resourceRoot), "ms0:/PSP/GAME/%s/", TITLE_ID_PSP);
    m_resourceToken = m_resourceRoot;
    snprintf(m_writableRoot, sizeof(m_writableRoot), "%s", m_resourceRoot);
}

bool PspPlatform::AppendResolved(const char* root, const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!root || !relativePath || !outBuf || bufSize == 0)
        return false;

    while (*relativePath == '/' || *relativePath == '\\')
        ++relativePath;

    const size_t rootLength = strlen(root);
    const char* separator = (rootLength == 0 || root[rootLength - 1] == ':' || root[rootLength - 1] == '/') ? "" : "/";

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

bool PspPlatform::BuildPath(const char* relativePath, char* outBuf, size_t bufSize) const { return AppendResolved(m_resourceToken, relativePath, outBuf, bufSize); }

bool PspPlatform::BuildWritablePath(const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!m_writableRoot[0])
        return false;

    sceIoMkdir(m_writableRoot, 0777);
    return AppendResolved(m_writableRoot, relativePath, outBuf, bufSize);
}

FileHandle PspPlatform::FileOpen(const char* path, FileMode mode)
{
    if (!path)
        return nullptr;

    SceUID fd;
    if (mode == FileMode::Write)
        fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    else
        fd = sceIoOpen(path, PSP_O_RDONLY, 0777);

    if (fd < 0)
        return nullptr;
    return ToHandle(fd);
}

bool PspPlatform::FileSeek(FileHandle file, uint64_t offset)
{
    if (!file)
        return false;
    return sceIoLseek(ToFd(file), static_cast<SceOff>(offset), PSP_SEEK_SET) >= 0;
}

size_t PspPlatform::FileRead(FileHandle file, void* dst, size_t bytes)
{
    if (!file || !dst)
        return 0;
    const int read = sceIoRead(ToFd(file), dst, static_cast<SceSize>(bytes));
    return (read < 0) ? 0u : static_cast<size_t>(read);
}

size_t PspPlatform::FileWrite(FileHandle file, const void* src, size_t bytes)
{
    if (!file || !src)
        return 0;
    const int written = sceIoWrite(ToFd(file), src, static_cast<SceSize>(bytes));
    return (written < 0) ? 0u : static_cast<size_t>(written);
}

uint64_t PspPlatform::FileSize(FileHandle file) const
{
    if (!file)
        return 0;

    const SceUID fd = ToFd(file);
    const SceOff current = sceIoLseek(fd, 0, PSP_SEEK_CUR);
    if (current < 0)
        return 0;

    const SceOff size = sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, current, PSP_SEEK_SET);
    return (size < 0) ? 0u : static_cast<uint64_t>(size);
}

void PspPlatform::FileClose(FileHandle file)
{
    if (file)
        sceIoClose(ToFd(file));
}
