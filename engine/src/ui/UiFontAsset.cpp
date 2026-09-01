#include "UiInternal.h"

#include "../../include/core/EngineDebug.h"
#include "../../include/core/EngineIO.h"
#include "../../include/core/EngineSubsystems.h"
#include "../../include/resources/EngineResource.h"
#include "Engine.h"
#include "graphics/Types.h"

namespace
{
    const char* const FONT_ASSET = "RASSETS\\ENGINE_FONT.PS2A";

    enum class FontState : uint8_t
    {
        Unrequested = 0,
        Streaming,
        Ready,
        Unavailable
    };

    int32_t s_Handle = -1;
    const Font* s_Font = nullptr;
    uint32_t s_Atlas = 0;
    FontState s_State = FontState::Unrequested;
    bool s_ReportedUnavailable = false;
    bool s_ReportedReady = false;

    void Unavailable(const char* why)
    {
        s_State = FontState::Unavailable;
        s_Font = nullptr;
        s_Atlas = 0;
        if (!s_ReportedUnavailable)
        {
            s_ReportedUnavailable = true;
            Engine_LogInfo("Ui: drawing with the built-in font (%s).", why);
        }
    }

    void Request()
    {
        if (!Engine_Subsystem_IsEnabled(EngineSubsystem::Resource))
        {
            Unavailable("the resource subsystem is not loaded");
            return;
        }

        char path[IO_FILE_MAX_PATH];
        if (!Engine_BuildPath(Engine_GetResourceLocationToken(), FONT_ASSET, path, sizeof(path)))
        {
            Unavailable("the font asset path could not be built");
            return;
        }

        s_Handle = Engine_Resource_Load(RES_FONT, path);
        if (s_Handle < 0)
        {
            Unavailable("no cooked font is present");
            return;
        }

        Engine_Resource_Pin(s_Handle);
        s_State = FontState::Streaming;
    }
} // namespace

void UiInternal_UpdateFont()
{
    if (s_State == FontState::Unrequested)
        Request();

    if (s_State == FontState::Streaming)
    {
        if (!Engine_Resource_IsReady(s_Handle))
            return;

        const Font* font = static_cast<const Font*>(Engine_Resource_Get(s_Handle));
        if (!font || !font->glyphs)
        {
            Unavailable("the cooked font could not be decoded");
            return;
        }
        s_Font = font;
        s_State = FontState::Ready;
    }

    // Every path below reads the font, so nothing may reach them without one.
    if (s_State != FontState::Ready || !s_Font)
        return;

    // The atlas streams separately and is charged against the texture budget, so
    // it arrives later than the metrics and can still be refused. Waiting and
    // failing look the same from a handle alone, so ask the slot which it is:
    // a slot that is gone means the load failed, and only that is terminal.
    int atlasW = 0;
    int atlasH = 0;
    const UiTextureState atlasState = UiInternal_ResolveTexture(s_Font->atlasResourceId, &s_Atlas, &atlasW, &atlasH);
    if (atlasState == UiTextureState::Loading)
        return;
    if (atlasState != UiTextureState::Ready)
    {
        Unavailable("its atlas could not be loaded");
        return;
    }

    if (!s_ReportedReady)
    {
        s_ReportedReady = true;
        Engine_LogInfo("Ui: cooked font ready, %u glyphs from a %ux%u atlas.", s_Font->glyphCount, s_Font->atlasWidth, s_Font->atlasHeight);
    }
}

void UiInternal_FontShutdown()
{
    if (s_Handle >= 0)
    {
        Engine_Resource_Unpin(s_Handle);
        Engine_Resource_Unload(s_Handle);
    }
    s_Handle = -1;
    s_Font = nullptr;
    s_Atlas = 0;
    s_State = FontState::Unrequested;
    s_ReportedUnavailable = false;
    s_ReportedReady = false;
}

void UiInternal_FontForget()
{
    // The runtime reset releases every resource, pinned ones included, so the
    // handle this holds is stale rather than merely unused.
    s_Handle = -1;
    s_Font = nullptr;
    s_Atlas = 0;
    s_State = FontState::Unrequested;
    s_ReportedReady = false;
}

const Font* UiInternal_CookedFont() { return (s_State == FontState::Ready && s_Atlas != 0) ? s_Font : nullptr; }

uint32_t UiInternal_AtlasTexture() { return s_Atlas; }

bool Ui_FontIsCooked() { return UiInternal_CookedFont() != nullptr; }

UiTextureState UiInternal_ResolveTexture(int32_t handle, uint32_t* outTexture, int* outWidth, int* outHeight)
{
    *outTexture = 0;
    *outWidth = 0;
    *outHeight = 0;
    if (handle < 0)
        return UiTextureState::Absent;

    ResourceInfo info;
    if (!Engine_Resource_GetInfo(handle, &info) || info.type != RES_TEXTURE)
        return UiTextureState::Absent;
    if (info.state != RES_STATE_READY)
        return UiTextureState::Loading;

    const Texture2D* texture = static_cast<const Texture2D*>(Engine_Resource_Get(handle));
    if (!texture || texture->id == 0)
        return UiTextureState::Absent;

    *outTexture = texture->id;
    *outWidth = info.width;
    *outHeight = info.height;
    return UiTextureState::Ready;
}
