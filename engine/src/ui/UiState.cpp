#include "UiInternal.h"

#include <cstring>

#include "../../include/core/EngineDebug.h"

namespace
{
    const uint32_t FNV_OFFSET_BASIS = 2166136261u;
    const uint32_t FNV_PRIME = 16777619u;

    // A widget skipped for a single frame by a paging branch must not lose what
    // it remembers, so an entry is only reusable once it has been unseen for
    // longer than that.
    const uint32_t STALE_FRAMES = 2;

    // Linear probing, bounded. A scan of the whole table per widget per frame is
    // not affordable on the constrained platform.
    const uint32_t PROBE_LIMIT = 8;

    UiState s_States[UI_MAX_STATES];
    UiState s_Scratch;
    uint32_t s_Frame = 0;
    uint32_t s_Used = 0;
    bool s_FullReported = false;
} // namespace

void UiInternal_StateBeginFrame()
{
    ++s_Frame;
    s_FullReported = false;

    s_Used = 0;
    for (uint32_t i = 0; i < UI_MAX_STATES; ++i)
    {
        if (s_States[i].id != 0)
            ++s_Used;
    }
}

void UiInternal_StateReset()
{
    memset(s_States, 0, sizeof(s_States));
    memset(&s_Scratch, 0, sizeof(s_Scratch));
    s_Frame = 0;
    s_Used = 0;
    s_FullReported = false;
}

UiState* UiInternal_StateFor(uint32_t id)
{
    if (id == 0)
        return &s_Scratch;

    const uint32_t base = id % UI_MAX_STATES;

    for (uint32_t probe = 0; probe < PROBE_LIMIT; ++probe)
    {
        UiState& entry = s_States[(base + probe) % UI_MAX_STATES];
        if (entry.id == id)
        {
            entry.touchedFrame = s_Frame;
            return &entry;
        }
    }

    // No entry yet: take a free one, then the stalest one that has not been
    // asked for recently, and only then give up.
    UiState* candidate = nullptr;
    uint32_t oldest = s_Frame;
    for (uint32_t probe = 0; probe < PROBE_LIMIT; ++probe)
    {
        UiState& entry = s_States[(base + probe) % UI_MAX_STATES];
        if (entry.id == 0)
        {
            candidate = &entry;
            break;
        }
        if (entry.touchedFrame + STALE_FRAMES < s_Frame && entry.touchedFrame <= oldest)
        {
            oldest = entry.touchedFrame;
            candidate = &entry;
        }
    }

    if (!candidate)
    {
        if (!s_FullReported)
        {
            s_FullReported = true;
            Engine_LogError("Ui: interaction state store full (%u entries); widgets past it still draw but remember nothing.", static_cast<unsigned>(UI_MAX_STATES));
        }
        memset(&s_Scratch, 0, sizeof(s_Scratch));
        return &s_Scratch;
    }

    memset(candidate, 0, sizeof(*candidate));
    candidate->id = id;
    candidate->touchedFrame = s_Frame;
    return candidate;
}

uint32_t UiInternal_StateHash(uint32_t seed, const char* text)
{
    uint32_t hash = seed ? seed : FNV_OFFSET_BASIS;
    for (const char* p = text; p && *p; ++p)
    {
        hash ^= static_cast<uint32_t>(static_cast<unsigned char>(*p));
        hash *= FNV_PRIME;
    }
    return hash ? hash : 1u;
}

uint32_t Ui_StatesUsed() { return s_Used; }

uint32_t Ui_StateBudget() { return UI_MAX_STATES; }
