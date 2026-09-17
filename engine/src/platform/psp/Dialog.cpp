#include <cstdio>
#include <cstring>

#include "Platform.h"
#include "UtilityDialog.h"
#include "core/EngineDebug.h"

extern "C" {
#include <psputility.h>
}

namespace
{
    enum : uint32_t
    {
        OSK_TEXT_MAX = 128,
        OSK_DESC_MAX = 64
    };

    // The services read these for as long as the dialog is open, so they
    // outlive Dialog_Open rather than living on its stack.
    pspUtilityMsgDialogParams s_MsgParams;
    SceUtilityOskParams s_OskParams;
    SceUtilityOskData s_OskData;
    unsigned short s_OskDesc[OSK_DESC_MAX];
    unsigned short s_OskIn[OSK_TEXT_MAX];
    unsigned short s_OskOut[OSK_TEXT_MAX];

    void FillCommon(pspUtilityDialogCommon* base, unsigned int size)
    {
        memset(base, 0, sizeof(*base));
        base->size = size;
        sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &base->language);
        sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN, &base->buttonSwap);
        base->graphicsThread = 0x11;
        base->accessThread = 0x13;
        base->fontThread = 0x12;
        base->soundThread = 0x10;
    }

    // The services speak UTF-16; the engine speaks printable ASCII. Anything
    // outside that set is dropped in both directions rather than guessed at.
    void AsciiToWide(const char* src, unsigned short* dst, uint32_t dstCount)
    {
        uint32_t n = 0;
        if (src)
        {
            for (; src[n] != '\0' && n + 1u < dstCount; ++n)
            {
                const unsigned char c = static_cast<unsigned char>(src[n]);
                dst[n] = (c >= 32u && c <= 126u) ? static_cast<unsigned short>(c) : static_cast<unsigned short>('?');
            }
        }
        dst[n] = 0;
    }

    void WideToAscii(const unsigned short* src, char* dst, size_t dstSize)
    {
        if (!dst || dstSize == 0)
            return;

        size_t n = 0;
        for (; src && src[n] != 0 && n + 1u < dstSize; ++n)
        {
            const unsigned short c = src[n];
            dst[n] = (c >= 32u && c <= 126u) ? static_cast<char>(c) : '?';
        }
        dst[n] = '\0';
    }
} // namespace

bool PspPlatform::Dialog_Open(const DialogRequest& request)
{
    if (PspUtilityDialog_IsActive())
        return false;

    m_dialogResultBuffer = nullptr;
    m_dialogResultBufferSize = 0;

    if (request.kind == DialogKind::Message || request.kind == DialogKind::Confirm)
    {
        FillCommon(&s_MsgParams.base, sizeof(s_MsgParams));
        s_MsgParams.mode = PSP_UTILITY_MSGDIALOG_MODE_TEXT;
        s_MsgParams.options = PSP_UTILITY_MSGDIALOG_OPTION_TEXT;
        if (request.kind == DialogKind::Confirm)
            s_MsgParams.options |= PSP_UTILITY_MSGDIALOG_OPTION_YESNO_BUTTONS | PSP_UTILITY_MSGDIALOG_OPTION_DEFAULT_NO;

        const char* body = request.body ? request.body : "";
        snprintf(s_MsgParams.message, sizeof(s_MsgParams.message), "%s", body);

        const int rc = sceUtilityMsgDialogInitStart(&s_MsgParams);
        if (rc < 0)
        {
            Engine_LogError("%s: sceUtilityMsgDialogInitStart failed (0x%08X)", GetName(), static_cast<unsigned>(rc));
            return false;
        }

        m_dialogKind = request.kind;
        PspUtilityDialog_SetService(PspDialogService::Message);
        PspUtilityDialog_SetActive(true);
        return true;
    }

    if (request.kind == DialogKind::TextInput)
    {
        if (!request.textBuffer || request.textBufferSize == 0)
            return false;

        AsciiToWide(request.title, s_OskDesc, OSK_DESC_MAX);
        AsciiToWide(request.textBuffer, s_OskIn, OSK_TEXT_MAX);
        memset(s_OskOut, 0, sizeof(s_OskOut));

        memset(&s_OskData, 0, sizeof(s_OskData));
        s_OskData.language = PSP_UTILITY_OSK_LANGUAGE_DEFAULT;
        s_OskData.lines = 1;
        s_OskData.unk_24 = 1;
        s_OskData.inputtype = PSP_UTILITY_OSK_INPUTTYPE_ALL;
        s_OskData.desc = s_OskDesc;
        s_OskData.intext = s_OskIn;
        s_OskData.outtextlength = OSK_TEXT_MAX;
        s_OskData.outtextlimit = static_cast<int>((request.textBufferSize < OSK_TEXT_MAX) ? request.textBufferSize - 1u : OSK_TEXT_MAX - 1u);
        s_OskData.outtext = s_OskOut;

        FillCommon(&s_OskParams.base, sizeof(s_OskParams));
        s_OskParams.datacount = 1;
        s_OskParams.data = &s_OskData;

        const int rc = sceUtilityOskInitStart(&s_OskParams);
        if (rc < 0)
        {
            Engine_LogError("%s: sceUtilityOskInitStart failed (0x%08X)", GetName(), static_cast<unsigned>(rc));
            return false;
        }

        m_dialogKind = request.kind;
        m_dialogResultBuffer = request.textBuffer;
        m_dialogResultBufferSize = request.textBufferSize;
        PspUtilityDialog_SetService(PspDialogService::Osk);
        PspUtilityDialog_SetActive(true);
        return true;
    }

    return false;
}

DialogStatus PspPlatform::Dialog_Poll()
{
    if (!PspUtilityDialog_IsActive())
        return DialogStatus::Idle;

    const bool isOsk = PspUtilityDialog_Service() == PspDialogService::Osk;
    const int status = isOsk ? sceUtilityOskGetStatus() : sceUtilityMsgDialogGetStatus();

    switch (status)
    {
    case PSP_UTILITY_DIALOG_NONE:
    case PSP_UTILITY_DIALOG_INIT:
    case PSP_UTILITY_DIALOG_VISIBLE:
        return DialogStatus::Pending;

    case PSP_UTILITY_DIALOG_QUIT:
        if (isOsk)
            sceUtilityOskShutdownStart();
        else
            sceUtilityMsgDialogShutdownStart();
        return DialogStatus::Pending;

    default:
        break;
    }

    DialogStatus result = DialogStatus::Cancelled;

    if (isOsk)
    {
        if (s_OskData.result != PSP_UTILITY_OSK_RESULT_CANCELLED && m_dialogResultBuffer)
        {
            WideToAscii(s_OskOut, m_dialogResultBuffer, m_dialogResultBufferSize);
            result = DialogStatus::Accepted;
        }
    }
    else if (m_dialogKind == DialogKind::Confirm)
    {
        result = (s_MsgParams.buttonPressed == PSP_UTILITY_MSGDIALOG_RESULT_YES) ? DialogStatus::Accepted : DialogStatus::Cancelled;
    }
    else
    {
        result = (s_MsgParams.buttonPressed == PSP_UTILITY_MSGDIALOG_RESULT_BACK) ? DialogStatus::Cancelled : DialogStatus::Accepted;
    }

    PspUtilityDialog_SetActive(false);
    PspUtilityDialog_SetService(PspDialogService::None);
    m_dialogKind = DialogKind::Count;
    m_dialogResultBuffer = nullptr;
    m_dialogResultBufferSize = 0;
    return result;
}

void PspPlatform::Dialog_Cancel()
{
    if (!PspUtilityDialog_IsActive())
        return;

    if (PspUtilityDialog_Service() == PspDialogService::Osk)
        sceUtilityOskShutdownStart();
    else
        sceUtilityMsgDialogShutdownStart();

    PspUtilityDialog_SetActive(false);
    PspUtilityDialog_SetService(PspDialogService::None);
    m_dialogKind = DialogKind::Count;
    m_dialogResultBuffer = nullptr;
    m_dialogResultBufferSize = 0;
}
