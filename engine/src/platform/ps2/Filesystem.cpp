#include <cstdio>
#include <cstring>

#include "Platform.h"
#include "Ps2SaveIcon.h"
#include "TitleInfo.h"
#include "core/EngineDebug.h"
#include "core/EngineIO.h"

#include <libmc.h>
#include <loadfile.h>

// ---------------------------------------------------------------------------
// PS2 storage. Files are plain stdio; what is platform-specific is the device
// token grammar - cdrom0: wants a ";1" version suffix, host: is relative to the
// directory the ELF was launched from, mass0:/hdd0: take the path as-is.
// ---------------------------------------------------------------------------

namespace
{
    const int MC_PORTS = 2;
    const int MC_SLOT = 0;
    const int MC_SAME_CARD = 0;
    const int MC_FORMATTED_CARD_INSERTED = -1;

    bool s_CardChecked = false;
    int s_CardPort = -1;

    bool CardUsable(int port)
    {
        int type = 0;
        int freeClusters = 0;
        int formatted = 0;
        if (mcGetInfo(port, MC_SLOT, &type, &freeClusters, &formatted) < 0)
            return false;

        int result = 0;
        mcSync(0, nullptr, &result);
        if (result != MC_SAME_CARD && result != MC_FORMATTED_CARD_INSERTED)
        {
            Engine_LogInfo("PS2 storage: no usable card in slot %d (status %d)", port, result);
            return false;
        }

        Engine_LogInfo("PS2 storage: card in slot %d, %d free clusters", port, freeClusters);
        return true;
    }

    int FindCard()
    {
        if (s_CardChecked)
            return s_CardPort;
        s_CardChecked = true;

        if (SifLoadModule("rom0:SIO2MAN", 0, nullptr) < 0)
        {
            Engine_LogError("PS2 storage: SIO2MAN failed to load; no card access");
            return -1;
        }
        if (SifLoadModule("rom0:MCMAN", 0, nullptr) < 0)
        {
            Engine_LogError("PS2 storage: MCMAN failed to load; no card access");
            return -1;
        }
        if (SifLoadModule("rom0:MCSERV", 0, nullptr) < 0)
        {
            Engine_LogError("PS2 storage: MCSERV failed to load; no card access");
            return -1;
        }
        if (mcInit(MC_TYPE_MC) < 0)
        {
            Engine_LogError("PS2 storage: the card library would not start; no card access");
            return -1;
        }

        for (int port = 0; port < MC_PORTS; ++port)
        {
            if (CardUsable(port))
            {
                s_CardPort = port;
                return port;
            }
        }

        Engine_LogInfo("PS2 storage: no memory card; nothing will be saved this session");
        return -1;
    }

    const char* const kTokenCdrom = "cdrom0:";
    const char* const kTokenMass = "mass0:";
    const char* const kTokenHdd = "hdd0:";
    const char* const kTokenHost = "host:";
} // namespace

const char* Ps2Platform::ResolveDeviceToken(const char* bootPath)
{
    // argv[0] looks like "cdrom0:\MAIN.ELF;1" or "host:main.elf". Match only the
    // leading device name, never the whole path.
    if (!bootPath || bootPath[0] == '\0')
        return kTokenCdrom;

    if (bootPath[0] == 'c')
        return kTokenCdrom;
    if (bootPath[0] == 'm' && bootPath[1] == 'a')
        return kTokenMass;
    if (bootPath[0] == 'h' && bootPath[1] == 'd')
        return kTokenHdd;
    if (bootPath[0] == 'h' && bootPath[1] == 'o')
        return kTokenHost;

    // Unrecognised device: fall back to the disc rather than refusing to boot.
    return kTokenCdrom;
}

bool Ps2Platform::BuildPath(const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!relativePath || !outBuf || bufSize == 0)
        return false;

    int written = 0;
    const char* token = m_deviceToken;

    if (token[0] == 'c') // cdrom0: -> "cdrom0:<PATH>;1"
    {
        written = snprintf(outBuf, bufSize, "cdrom0:%s;1", relativePath);
    }
    else if (token[0] == 'm') // mass0:
    {
        written = snprintf(outBuf, bufSize, "mass0:%s", relativePath);
    }
    else if (token[0] == 'h' && token[1] == 'd') // hdd0:
    {
        written = snprintf(outBuf, bufSize, "hdd0:%s", relativePath);
    }
    else if (token[0] == 'h' && token[1] == 'o') // host: -> relative to the ELF's dir
    {
        const char* bootPath = m_startupArgs.commandLine ? m_startupArgs.commandLine->GetPositional(0) : nullptr;
        char baseDir[IO_FILE_MAX_PATH];
        strncpy(baseDir, bootPath ? bootPath : kTokenHost, sizeof(baseDir) - 1);
        baseDir[sizeof(baseDir) - 1] = '\0';

        char* lastSlash = strrchr(baseDir, '/');
        char* lastBackslash = strrchr(baseDir, '\\');
        char* lastColon = strchr(baseDir, ':');

        char* splitPoint = (lastSlash > lastBackslash) ? lastSlash : lastBackslash;
        if (!splitPoint)
            splitPoint = lastColon;

        if (splitPoint)
        {
            *(splitPoint + 1) = '\0'; // keep the separator
            written = snprintf(outBuf, bufSize, "%s%s", baseDir, relativePath);
        }
        else
        {
            written = snprintf(outBuf, bufSize, "%s%s", kTokenHost, relativePath);
        }
    }

    if (written < 0 || static_cast<size_t>(written) >= bufSize)
        return false;

    // Normalise separators: the IOP filesystem drivers accept '/' everywhere,
    // and the baked asset keys use it.
    for (int i = 0; i < written; ++i)
    {
        if (outBuf[i] == '\\')
            outBuf[i] = '/';
    }
    return true;
}

namespace
{
    /// Write the descriptor and icon a memory card browser needs, once.
    ///
    /// A save directory without them is reported as corrupted by the console's
    /// browser even when its data is perfectly readable, so the player cannot
    /// see or delete it. Written only when absent, so this costs one failed
    /// open per boot rather than a rewrite.
    /// @param port Memory card port the save lives on.
    /// @param directory The save directory, already created.
    void EnsureSaveIcon(int port, const char* directory)
    {
        // The directory is the title id, so it is short; bound it anyway rather
        // than let a longer one silently truncate into a different path.
        char safeDir[64];
        std::strncpy(safeDir, directory, sizeof(safeDir) - 1);
        safeDir[sizeof(safeDir) - 1] = '\0';

        char path[IO_FILE_MAX_PATH];

        snprintf(path, sizeof(path), "mc%d:%s/icon.sys", port, safeDir);
        if (FILE* existing = fopen(path, "rb"))
        {
            fclose(existing);
            return;
        }

        if (FILE* sys = fopen(path, "wb"))
        {
            fwrite(PS2_SAVE_ICON_SYS, 1, sizeof(PS2_SAVE_ICON_SYS), sys);
            fclose(sys);
        }
        else
        {
            Engine_LogError("PS2 storage: could not write '%s'; the browser will report this save corrupted.", path);
            return;
        }

        snprintf(path, sizeof(path), "mc%d:%s/%s", port, safeDir, PS2_SAVE_ICON_NAME);
        FILE* icon = fopen(path, "wb");
        if (!icon)
        {
            Engine_LogError("PS2 storage: could not write '%s'; the browser will report this save corrupted.", path);
            return;
        }

        fwrite(PS2_SAVE_ICON_MODEL, 1, sizeof(PS2_SAVE_ICON_MODEL), icon);

        // The model declares an uncompressed texture; a single colour repeated
        // is written here rather than stored, which keeps 32 KB of identical
        // bytes out of the binary.
        const uint16_t texel = PS2_SAVE_ICON_TEXEL;
        uint16_t row[PS2_SAVE_ICON_TEX_DIM];
        for (int i = 0; i < PS2_SAVE_ICON_TEX_DIM; ++i)
            row[i] = texel;
        for (int y = 0; y < PS2_SAVE_ICON_TEX_DIM; ++y)
            fwrite(row, sizeof(uint16_t), PS2_SAVE_ICON_TEX_DIM, icon);

        fclose(icon);
        Engine_LogInfo("PS2 storage: wrote the save icon for '%s'.", safeDir);
    }
} // namespace

bool Ps2Platform::BuildWritablePath(const char* relativePath, char* outBuf, size_t bufSize) const
{
    if (!relativePath || !outBuf || bufSize == 0)
        return false;

    const int port = FindCard();
    if (port < 0)
        return false;

    while (*relativePath == '/' || *relativePath == 0x5C)
        ++relativePath;

    char directory[IO_FILE_MAX_PATH];
    if (snprintf(directory, sizeof(directory), "/%sA", TITLE_ID_PS2) >= static_cast<int>(sizeof(directory)))
        return false;

    mcMkDir(port, MC_SLOT, directory);
    int result = 0;
    mcSync(0, nullptr, &result);

    EnsureSaveIcon(port, directory);

    const int written = snprintf(outBuf, bufSize, "mc%d:%s/%s", port, directory, relativePath);
    return written >= 0 && static_cast<size_t>(written) < bufSize;
}

FileHandle Ps2Platform::FileOpen(const char* path, FileMode mode)
{
    if (!path)
        return nullptr;
    FILE* file = fopen(path, mode == FileMode::Write ? "wb" : "rb");
    return reinterpret_cast<FileHandle>(file);
}

bool Ps2Platform::FileSeek(FileHandle file, uint64_t offset)
{
    if (!file)
        return false;
    return fseek(reinterpret_cast<FILE*>(file), static_cast<long>(offset), SEEK_SET) == 0;
}

size_t Ps2Platform::FileRead(FileHandle file, void* dst, size_t bytes)
{
    if (!file || !dst)
        return 0;
    return fread(dst, 1, bytes, reinterpret_cast<FILE*>(file));
}

size_t Ps2Platform::FileWrite(FileHandle file, const void* src, size_t bytes)
{
    if (!file || !src)
        return 0;
    return fwrite(src, 1, bytes, reinterpret_cast<FILE*>(file));
}

uint64_t Ps2Platform::FileSize(FileHandle file) const
{
    if (!file)
        return 0;

    FILE* f = reinterpret_cast<FILE*>(file);
    const long current = ftell(f);
    if (current < 0 || fseek(f, 0, SEEK_END) != 0)
        return 0;

    const long size = ftell(f);
    fseek(f, current, SEEK_SET);
    return (size < 0) ? 0 : static_cast<uint64_t>(size);
}

void Ps2Platform::FileClose(FileHandle file)
{
    if (file)
        fclose(reinterpret_cast<FILE*>(file));
}
