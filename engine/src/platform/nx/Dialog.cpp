#include <cstring>

#include "Platform.h"
#include "core/EngineDebug.h"

#include <switch.h>

namespace
{
    const size_t NX_UTF8_MAX_BYTES_PER_CHARACTER = 4;

    void Utf8ToAscii(const char* src, char* dst, size_t dstSize)
    {
        if (!dst || dstSize == 0)
            return;

        size_t n = 0;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(src); p && *p != '\0' && n + 1u < dstSize; ++p)
        {
            if (*p >= 32u && *p <= 126u)
                dst[n++] = static_cast<char>(*p);
        }
        dst[n] = '\0';
    }
} // namespace

bool NxPlatform::Dialog_Open(const DialogRequest& request)
{
    if (request.kind != DialogKind::TextInput)
        return false;
    if (!request.textBuffer || request.textBufferSize < 2u)
        return false;

    SwkbdConfig config;
    Result rc = swkbdCreate(&config, 0);
    if (R_FAILED(rc))
    {
        Engine_LogError("%s: swkbdCreate failed (0x%X)", GetName(), static_cast<unsigned>(rc));
        return false;
    }

    const size_t capacity = request.textBufferSize - 1u;
    const size_t maxCharacters = (capacity < UI_TEXT_INPUT_MAX) ? capacity : UI_TEXT_INPUT_MAX;

    swkbdConfigMakePresetDefault(&config);
    if (request.title)
    {
        swkbdConfigSetHeaderText(&config, request.title);
        swkbdConfigSetGuideText(&config, request.title);
    }
    swkbdConfigSetInitialText(&config, request.textBuffer);
    swkbdConfigSetStringLenMax(&config, static_cast<u32>(maxCharacters));

    char utf8[UI_TEXT_INPUT_MAX * NX_UTF8_MAX_BYTES_PER_CHARACTER + 1u];
    memset(utf8, 0, sizeof(utf8));
    rc = swkbdShow(&config, utf8, sizeof(utf8));
    swkbdClose(&config);

    if (R_SUCCEEDED(rc))
    {
        Utf8ToAscii(utf8, request.textBuffer, request.textBufferSize);
        m_dialogResult = DialogStatus::Accepted;
    }
    else
    {
        m_dialogResult = DialogStatus::Cancelled;
    }
    return true;
}

DialogStatus NxPlatform::Dialog_Poll()
{
    const DialogStatus result = m_dialogResult;
    if (result == DialogStatus::Accepted || result == DialogStatus::Cancelled)
        m_dialogResult = DialogStatus::Idle;
    return result;
}

void NxPlatform::Dialog_Cancel() { m_dialogResult = DialogStatus::Idle; }
