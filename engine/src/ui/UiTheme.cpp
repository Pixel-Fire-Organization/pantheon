#include "UiInternal.h"

#include <cstring>

#include "../../include/core/EngineDebug.h"
#include "../../include/core/EngineIO.h"
#include "../../include/core/EngineSubsystems.h"
#include "../../include/resources/EngineResource.h"
#include "Engine.h"
#include "graphics/ThemeFormat.h"

static_assert(sizeof(UiStyle) == THEME_STYLE_BYTES, "UiStyle is an on-disc contract; its size may not drift from the theme format");
static_assert(offsetof(UiTheme, style) == 0, "A decoded theme leads with its style block");

namespace
{
    const uint32_t FNV_OFFSET_BASIS = 2166136261u;
    const uint32_t FNV_PRIME = 16777619u;

    char s_ThemeName[THEME_KEY_MAX] = {0};
    char s_RoleFont[static_cast<uint8_t>(UiFontRole::Count)][THEME_KEY_MAX] = {{0}};

    uint16_t ReadU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

    uint32_t ReadU32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24); }

    uint32_t Checksum(const uint8_t* data, size_t size)
    {
        uint32_t hash = FNV_OFFSET_BASIS;
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= data[i];
            hash *= FNV_PRIME;
        }
        return hash;
    }

    bool InRange(int value, int low, int high) { return value >= low && value <= high; }

    /// A checksum proves a file arrived as it was written; it says nothing about
    /// whether it was written sensibly. A zero text scale divides by zero in
    /// every routine that fits text to a width, and a negative padding inverts
    /// every row rectangle computed from it.
    /// @param style The staged style, not yet committed.
    /// @param key The asset key, for the report.
    /// @return Whether every value is usable.
    bool MetricsAreSane(const UiStyle& style, const char* key)
    {
        struct Check
        {
            const char* name;
            int value;
            int low;
            int high;
        };
        const Check checks[] = {
            {"panelPadding", style.panelPadding, 0, 256},   {"itemSpacing", style.itemSpacing, 0, 256},
            {"borderWidth", style.borderWidth, 0, 64},      {"textScale", style.textScale, 1, 16},
            {"rowPadding", style.rowPadding, 0, 256},       {"barHeight", style.barHeight, 1, 256},
            {"cursorSize", style.cursorSize, 1, 256},       {"screenMargin", style.screenMargin, 0, 512},
            {"panelGap", style.panelGap, 0, 512},           {"scrollBarWidth", style.scrollBarWidth, 1, 128},
            {"menuBarHeight", style.menuBarHeight, 1, 256}, {"caretWidth", style.caretWidth, 1, 32},
            {"iconSpacing", style.iconSpacing, 0, 128},
        };

        for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i)
        {
            if (!InRange(checks[i].value, checks[i].low, checks[i].high))
            {
                Engine_LogError("Ui: theme '%s' refused: %s is %d, outside %d..%d", key, checks[i].name, checks[i].value, checks[i].low, checks[i].high);
                return false;
            }
        }

        if (!(style.repeatDelaySeconds >= 0.01f && style.repeatDelaySeconds <= 5.0f))
        {
            Engine_LogError("Ui: theme '%s' refused: repeat delay is out of range", key);
            return false;
        }
        if (!(style.repeatIntervalSeconds >= 0.01f && style.repeatIntervalSeconds <= 5.0f))
        {
            Engine_LogError("Ui: theme '%s' refused: repeat interval is out of range", key);
            return false;
        }
        return true;
    }

    void CopyKey(char* dst, const char* src)
    {
        std::strncpy(dst, src ? src : "", THEME_KEY_MAX - 1);
        dst[THEME_KEY_MAX - 1] = '\0';
    }
} // namespace

void UiInternal_ThemeReset() { Ui_SetBuiltinTheme(UiBuiltinTheme::MIDNIGHT); }

void Ui_SetBuiltinTheme(UiBuiltinTheme theme)
{
    if (static_cast<uint8_t>(theme) >= static_cast<uint8_t>(UiBuiltinTheme::Count))
        return;

    Ui_SetStyle(Ui_BuiltinTheme(theme));
    CopyKey(s_ThemeName, Ui_BuiltinThemeName(theme));
    for (uint8_t i = 0; i < static_cast<uint8_t>(UiFontRole::Count); ++i)
        CopyKey(s_RoleFont[i], Ui_BuiltinRoleFont(theme, static_cast<UiFontRole>(i)));
}

const char* Ui_ThemeName() { return s_ThemeName; }

const char* Ui_RoleFont(UiFontRole role)
{
    const uint8_t index = static_cast<uint8_t>(role);
    return (index < static_cast<uint8_t>(UiFontRole::Count)) ? s_RoleFont[index] : "";
}

bool Ui_LoadTheme(const char* key)
{
    if (!key || !key[0])
        return false;

    if (!Engine_Subsystem_IsEnabled(EngineSubsystem::Resource))
    {
        Engine_LogError("Ui: theme '%s' refused: the resource subsystem is not loaded", key);
        return false;
    }

    char path[IO_FILE_MAX_PATH];
    if (!Engine_BuildPath(Engine_GetResourceLocationToken(), key, path, sizeof(path)))
    {
        Engine_LogError("Ui: theme '%s' refused: its path could not be built", key);
        return false;
    }

    const int32_t handle = Engine_Resource_Load(RES_THEME, path);
    if (handle < 0)
    {
        Engine_LogError("Ui: theme '%s' refused: it could not be loaded", key);
        return false;
    }

    // The theme streams like any other asset, but a caller asking for one on
    // command expects it applied or refused by the time the call returns.
    if (Engine_Subsystem_IsEnabled(EngineSubsystem::Io))
        Engine_IO_Drain();

    // The decode already validated identity, version, size and integrity; a
    // ready entry is therefore one whose payload can be trusted structurally.
    const UiTheme* staged = static_cast<const UiTheme*>(Engine_Resource_Get(handle));
    if (!staged)
    {
        Engine_LogError("Ui: theme '%s' refused: it did not become ready", key);
        Engine_Resource_Unload(handle);
        return false;
    }

    UiTheme candidate = *staged;
    if (!MetricsAreSane(candidate.style, key))
    {
        Engine_Resource_Unload(handle);
        return false;
    }

    Ui_SetStyle(candidate.style);
    for (uint8_t i = 0; i < static_cast<uint8_t>(UiFontRole::Count); ++i)
        CopyKey(s_RoleFont[i], candidate.roleFont[i]);
    CopyKey(s_ThemeName, key);
    Engine_Resource_Unload(handle);
    Engine_LogInfo("Ui: theme '%s' applied.", key);
    return true;
}

bool Ui_ThemeDecode(const void* data, size_t size, UiTheme* outTheme)
{
    if (!data || !outTheme || size < THEME_HEADER_SIZE)
    {
        Engine_LogError("Ui: theme payload is shorter than a header");
        return false;
    }

    const uint8_t* base = static_cast<const uint8_t*>(data);
    if (ReadU32(base) != THEME_MAGIC)
    {
        Engine_LogError("Ui: bad theme magic; this is not a cooked theme");
        return false;
    }

    const uint16_t version = ReadU16(base + 4);
    if (version != THEME_VERSION)
    {
        Engine_LogError("Ui: theme layout version %u, this build reads %u; re-cook the theme", version, static_cast<unsigned>(THEME_VERSION));
        return false;
    }

    const uint16_t styleBytes = ReadU16(base + 6);
    if (styleBytes != THEME_STYLE_BYTES)
    {
        Engine_LogError("Ui: theme style block is %u bytes, this build expects %u", styleBytes, static_cast<unsigned>(THEME_STYLE_BYTES));
        return false;
    }

    const uint32_t recorded = ReadU32(base + 8);
    const uint8_t fontCount = base[12];
    const size_t expected = static_cast<size_t>(THEME_HEADER_SIZE) + THEME_STYLE_BYTES + static_cast<size_t>(fontCount) * THEME_FONT_REF_SIZE;
    if (fontCount > THEME_MAX_FONT_REFS || size != expected)
    {
        Engine_LogError("Ui: theme payload is %u bytes, header describes %u", static_cast<unsigned>(size), static_cast<unsigned>(expected));
        return false;
    }

    const uint32_t actual = Checksum(base + THEME_HEADER_SIZE, size - THEME_HEADER_SIZE);
    if (actual != recorded)
    {
        Engine_LogError("Ui: theme checksum 0x%08X does not match the recorded 0x%08X", actual, recorded);
        return false;
    }

    // Every font reference is checked before anything is written, so a theme
    // naming a role this build does not have leaves outTheme untouched.
    const uint8_t* refs = base + THEME_HEADER_SIZE + THEME_STYLE_BYTES;
    for (uint8_t i = 0; i < fontCount; ++i)
    {
        const uint8_t role = refs[static_cast<size_t>(i) * THEME_FONT_REF_SIZE];
        if (role >= static_cast<uint8_t>(UiFontRole::Count))
        {
            Engine_LogError("Ui: theme names font role %u, which does not exist", role);
            return false;
        }
    }

    std::memcpy(&outTheme->style, base + THEME_HEADER_SIZE, THEME_STYLE_BYTES);
    std::memset(outTheme->roleFont, 0, sizeof(outTheme->roleFont));

    for (uint8_t i = 0; i < fontCount; ++i, refs += THEME_FONT_REF_SIZE)
    {
        char key[THEME_KEY_MAX];
        std::memcpy(key, refs + 1, THEME_KEY_MAX);
        key[THEME_KEY_MAX - 1] = '\0';
        CopyKey(outTheme->roleFont[refs[0]], key);
    }
    return true;
}
