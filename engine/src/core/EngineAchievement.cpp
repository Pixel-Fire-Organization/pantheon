#include "core/EngineAchievement.h"

#include <cstring>

#include "core/EngineDebug.h"
#include "core/EngineIO.h"
#include "core/EngineSubsystems.h"
#include "ui/EngineAchievementUi.h"
#include "platform/AchievementContract.h"
#include "platform/Platform.h"

namespace
{
    const uint32_t FNV_OFFSET_BASIS = 2166136261u;
    const uint32_t FNV_PRIME = 16777619u;
    const size_t RECORD_CHECKED_OFFSET = 8;

    AchievementContract* s_Contract = nullptr;
    AchievementRecord s_Record;

    bool s_Loaded = false;
    bool s_Persistent = false;
    bool s_ReportedNoStorage = false;
    bool s_ReportedMirrorRefused = false;
    bool s_ReportedNotLoaded = false;

    uint32_t Checksum(const AchievementRecord& record)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
        uint32_t hash = FNV_OFFSET_BASIS;
        for (size_t i = RECORD_CHECKED_OFFSET; i < sizeof(AchievementRecord); ++i)
        {
            hash ^= bytes[i];
            hash *= FNV_PRIME;
        }
        return hash;
    }

    void Clear()
    {
        memset(&s_Record, 0, sizeof(s_Record));
        s_Record.magic = ACHV_RECORD_MAGIC;
        s_Record.version = static_cast<uint16_t>(ACHV_RECORD_VERSION);
        s_Record.declared = static_cast<uint16_t>(Engine_Achievement_DeclaredCount());
    }

    bool RecordPath(char* outBuf, size_t bufSize)
    {
        Platform* platform = Engine_GetPlatform();
        return platform && platform->BuildWritablePath(ACHV_RECORD_FILE, outBuf, bufSize);
    }

    void Load()
    {
        Clear();

        char path[IO_FILE_MAX_PATH];
        if (!RecordPath(path, sizeof(path)))
        {
            s_Persistent = false;
            Engine_LogInfo("Achievement: no writable storage; unlocks are kept for this session only");
            return;
        }
        s_Persistent = true;

        Platform* platform = Engine_GetPlatform();
        FileHandle file = platform->FileOpen(path, FileMode::Read);
        if (!file)
            return;

        AchievementRecord loaded;
        const size_t read = platform->FileRead(file, &loaded, sizeof(loaded));
        platform->FileClose(file);

        if (read != sizeof(loaded))
        {
            Engine_LogError("Achievement: the record at '%s' is too short to be one; starting from nothing", path);
            return;
        }
        if (loaded.magic != ACHV_RECORD_MAGIC || loaded.version != ACHV_RECORD_VERSION)
        {
            Engine_LogError("Achievement: the record at '%s' is not one this build wrote; starting from nothing", path);
            return;
        }
        if (loaded.checksum != Checksum(loaded))
        {
            Engine_LogError("Achievement: the record at '%s' failed its checksum; starting from nothing", path);
            return;
        }

        memcpy(s_Record.unlocked, loaded.unlocked, sizeof(s_Record.unlocked));
    }

    bool Save()
    {
        if (!s_Persistent)
            return false;

        char path[IO_FILE_MAX_PATH];
        if (!RecordPath(path, sizeof(path)))
            return false;

        s_Record.declared = static_cast<uint16_t>(Engine_Achievement_DeclaredCount());
        s_Record.checksum = Checksum(s_Record);

        Platform* platform = Engine_GetPlatform();
        FileHandle file = platform->FileOpen(path, FileMode::Write);
        if (!file)
        {
            if (!s_ReportedNoStorage)
            {
                s_ReportedNoStorage = true;
                Engine_LogError("Achievement: cannot write '%s'; unlocks stand but will not survive a restart", path);
            }
            return false;
        }

        const size_t written = platform->FileWrite(file, &s_Record, sizeof(s_Record));
        platform->FileClose(file);

        if (written != sizeof(s_Record))
        {
            if (!s_ReportedNoStorage)
            {
                s_ReportedNoStorage = true;
                Engine_LogError("Achievement: wrote %u of %u bytes to '%s'; unlocks will not survive a restart",
                                static_cast<unsigned>(written), static_cast<unsigned>(sizeof(s_Record)), path);
            }
            return false;
        }
        return true;
    }

    bool TestBit(uint32_t id) { return (s_Record.unlocked[id >> 3] & (1u << (id & 7u))) != 0u; }

    void SetBit(uint32_t id) { s_Record.unlocked[id >> 3] |= static_cast<uint8_t>(1u << (id & 7u)); }

    void Mirror(uint32_t id)
    {
        if (!s_Contract || !s_Contract->IsAvailable())
            return;
        if (s_Contract->Unlock(id))
            return;

        if (!s_ReportedMirrorRefused)
        {
            s_ReportedMirrorRefused = true;
            Engine_LogInfo("Achievement: the platform refused achievement %u; the engine's record stands", id);
        }
    }
}

bool Engine_Achievement_Init(const char* commId)
{
    s_Loaded = true;
    s_Contract = nullptr;
    s_Persistent = false;
    s_ReportedNoStorage = false;
    s_ReportedMirrorRefused = false;
    s_ReportedNotLoaded = false;

    Load();

    Platform* platform = Engine_GetPlatform();
    if (platform)
    {
        s_Contract = platform->GetAchievements();
        if (s_Contract && !s_Contract->Init(commId))
            Engine_LogInfo("Achievement: %s will not mirror unlocks; the engine records them regardless",
                           platform->GetName());
    }

    Engine_LogInfo("Achievement: ready (%u declared, record %s)", Engine_Achievement_DeclaredCount(),
                   s_Persistent ? "on disc" : "in memory only");
    return true;
}

void Engine_Achievement_Close()
{
    if (s_Loaded && s_Persistent)
        Save();
    if (s_Contract)
        s_Contract->Shutdown();
    s_Contract = nullptr;
    s_Loaded = false;
    s_Persistent = false;
}

bool Engine_Achievement_IsAvailable() { return s_Loaded; }

bool Engine_Achievement_IsMirrored() { return s_Loaded && s_Contract && s_Contract->IsAvailable(); }

const char* Engine_Achievement_GetMirrorReason()
{
    if (!s_Loaded)
        return "ACHIEVEMENT SUBSYSTEM NOT REQUESTED";
    if (!s_Contract)
        return "THIS PLATFORM HAS NO ACHIEVEMENTS OF ITS OWN";
    if (s_Contract->IsAvailable())
        return nullptr;
    return s_Contract->GetUnavailableReason();
}

bool Engine_Achievement_IsPersistent() { return s_Loaded && s_Persistent; }

bool Engine_Achievement_PumpStartup()
{
    return (s_Loaded && s_Contract) ? s_Contract->PumpStartup() : false;
}

int32_t Engine_Achievement_GetConsoleCount() { return (s_Loaded && s_Contract) ? s_Contract->GetConsoleCount() : -1; }

bool Engine_Achievement_Unlock(uint32_t id)
{
    if (!s_Loaded)
    {
        if (!s_ReportedNotLoaded)
        {
            s_ReportedNotLoaded = true;
            Engine_LogInfo("Achievement: subsystem not loaded; unlocks are ignored");
        }
        return false;
    }

    const uint32_t declared = Engine_Achievement_DeclaredCount();
    if (id >= declared || id >= ACHV_MAX_ENTRIES)
    {
        Engine_LogError("Achievement: unlock of unknown achievement %u (declared: %u)", id, declared);
        return false;
    }

    if (TestBit(id))
        return true;

    SetBit(id);
    Save();
    Mirror(id);
    Engine_AchievementUi_Notify(id);

    Engine_LogInfo("Achievement: unlocked %u '%s'", id, Engine_Achievement_GetName(id));
    return true;
}

bool Engine_Achievement_IsUnlocked(uint32_t id)
{
    if (!s_Loaded || id >= ACHV_MAX_ENTRIES || id >= Engine_Achievement_DeclaredCount())
        return false;
    return TestBit(id);
}

uint32_t Engine_Achievement_GetCount() { return s_Loaded ? Engine_Achievement_DeclaredCount() : 0u; }
