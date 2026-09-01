#include "Platform.h"

#include <cstring>

#include "core/EngineDebug.h"
#include "Macros.h"

#include "CommonDialog.h"

extern "C"
{
#include <psp2/common_dialog.h>
}

namespace
{
    /// Printable ASCII only, both directions: this is the boundary the
    /// platform contract names -- a character neither side can represent is
    /// dropped here, never handed further.
    void AsciiToUtf16(const char* ascii, SceWChar16* out, uint32_t capacity)
    {
        uint32_t w = 0;
        for (const char* p = ascii; p && *p && w + 1 < capacity; ++p)
        {
            if (*p >= 32 && *p < 127)
                out[w++] = static_cast<SceWChar16>(static_cast<unsigned char>(*p));
        }
        out[w] = 0;
    }

    uint32_t Utf16ToAscii(const SceWChar16* text, char* out, uint32_t capacity)
    {
        if (capacity == 0)
            return 0;
        uint32_t w = 0;
        for (uint32_t i = 0; text[i] != 0 && w + 1 < capacity; ++i)
        {
            if (text[i] >= 32 && text[i] < 127)
                out[w++] = static_cast<char>(text[i]);
        }
        out[w] = '\0';
        return w;
    }

    bool ConfigureCommonDialog(const char* platformName)
    {
        SceCommonDialogConfigParam config;
        sceCommonDialogConfigParamInit(&config);
        const int rc = sceCommonDialogSetConfigParam(&config);
        if (rc < 0)
        {
            Engine_LogError("%s: the common dialog service refused its configuration, code %08X", platformName, static_cast<unsigned>(rc));
            return false;
        }
        return true;
    }
} // namespace

uint32_t VitaPlatform::Keyboard_PopCharacters(char* outBuffer, uint32_t bufferSize)
{
    UNUSED_VAR(outBuffer);
    UNUSED_VAR(bufferSize);
    return 0;
}

bool VitaPlatform::Dialog_Open(const DialogRequest& request)
{
    if (m_dialogKind != DialogKind::Count)
        return false; // one dialog at a time

    ConfigureCommonDialog(GetName());

    if (request.kind == DialogKind::TextInput)
    {
        if (!request.textBuffer)
            return false;

        AsciiToUtf16(request.title, m_imeTitle, SCE_IME_DIALOG_MAX_TITLE_LENGTH);
        AsciiToUtf16(request.textBuffer, m_imeInitial, DIALOG_IME_TEXT_MAX);
        memcpy(m_imeInput, m_imeInitial, sizeof(m_imeInput));

        SceImeDialogParam param;
        sceImeDialogParamInit(&param);
        param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
        param.languagesForced = 0;
        param.type = SCE_IME_TYPE_BASIC_LATIN;
        param.option = 0;
        param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
        param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
        param.title = m_imeTitle;
        param.maxTextLength = DIALOG_IME_TEXT_MAX - 1;
        param.initialText = m_imeInitial;
        param.inputTextBuffer = m_imeInput;

        const SceInt32 rc = sceImeDialogInit(&param);
        if (rc < 0)
        {
            Engine_LogError("%s: the console refused to open the text input dialog, code %08X", GetName(), static_cast<unsigned>(rc));
            return false;
        }

        m_dialogKind = DialogKind::TextInput;
        m_dialogResultBuffer = request.textBuffer;
        m_dialogResultBufferSize = request.textBufferSize;
        VitaCommonDialog_SetActive(true);
        return true;
    }

    strncpy(m_dialogBody, request.body ? request.body : "", sizeof(m_dialogBody) - 1);
    m_dialogBody[sizeof(m_dialogBody) - 1] = '\0';

    memset(&m_msgUserParam, 0, sizeof(m_msgUserParam));
    m_msgUserParam.buttonType = (request.kind == DialogKind::Confirm) ? SCE_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL : SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    m_msgUserParam.msg = reinterpret_cast<const SceChar8*>(m_dialogBody);

    SceMsgDialogParam param;
    sceMsgDialogParamInit(&param);
    param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &m_msgUserParam;

    const int rc = sceMsgDialogInit(&param);
    if (rc < 0)
    {
        Engine_LogError("%s: the console refused to open the message dialog, code %08X", GetName(), static_cast<unsigned>(rc));
        return false;
    }

    m_dialogKind = request.kind;
    VitaCommonDialog_SetActive(true);
    return true;
}

DialogStatus VitaPlatform::Dialog_Poll()
{
    if (m_dialogKind == DialogKind::Count)
        return DialogStatus::Idle;

    if (m_dialogKind == DialogKind::TextInput)
    {
        if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
            return DialogStatus::Pending;

        SceImeDialogResult result;
        memset(&result, 0, sizeof(result));
        sceImeDialogGetResult(&result);

        const bool accepted = (result.button == SCE_IME_DIALOG_BUTTON_ENTER);
        if (accepted && m_dialogResultBuffer)
            Utf16ToAscii(m_imeInput, m_dialogResultBuffer, static_cast<uint32_t>(m_dialogResultBufferSize));

        sceImeDialogTerm();
        m_dialogKind = DialogKind::Count;
        m_dialogResultBuffer = nullptr;
        VitaCommonDialog_SetActive(false);
        return accepted ? DialogStatus::Accepted : DialogStatus::Cancelled;
    }

    if (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
        return DialogStatus::Pending;

    SceMsgDialogResult result;
    memset(&result, 0, sizeof(result));
    sceMsgDialogGetResult(&result);

    const bool accepted = (result.buttonId == SCE_MSG_DIALOG_BUTTON_ID_OK);

    sceMsgDialogTerm();
    m_dialogKind = DialogKind::Count;
    VitaCommonDialog_SetActive(false);
    return accepted ? DialogStatus::Accepted : DialogStatus::Cancelled;
}

void VitaPlatform::Dialog_Cancel()
{
    if (m_dialogKind == DialogKind::Count)
        return;

    // Abort moves the dialog to FINISHED on a subsequent status check, the
    // same place a normal close leaves it, so the next Dialog_Poll tears it
    // down through the one path that already exists for that rather than a
    // second copy of it here.
    if (m_dialogKind == DialogKind::TextInput)
        sceImeDialogAbort();
    else
        sceMsgDialogAbort();
}
