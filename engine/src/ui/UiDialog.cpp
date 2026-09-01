#include "../../include/ui/EngineUi.h"

#include <cstdio>
#include <cstring>

#include "UiInternal.h"
#include "platform/Platform.h"

namespace
{
    // --- The drawn-fallback keyboard, shared by every mechanism-3 caller ---
    //
    // One flat sequence of forty keys, drawn wrapped into four rows of ten:
    // navigation is the ordinary row/run system every other grid in this
    // interface already uses (Left/Right within a row, Up/Down between rows),
    // not a second model built for this one widget. Only appending and
    // backspacing from the end are supported -- there is no mid-string caret
    // to place a key at.
    const char CHAR_KEYS[] = "1234567890QWERTYUIOPASDFGHJKLZXCVBNM";
    const int CHAR_KEY_COUNT = 36;
    const int KEY_SPACE = 36;
    const int KEY_BACKSPACE = 37;
    const int KEY_DONE = 38;
    const int KEY_CANCEL = 39;
    const int KEYBOARD_KEY_COUNT = 40;
    const int KEYBOARD_COLUMNS = 10;
    const int KEYBOARD_ROW_COUNT = KEYBOARD_KEY_COUNT / KEYBOARD_COLUMNS;

    enum class KeyAction : uint8_t
    {
        None,
        Committed,
        Cancelled
    };

    enum class CharAction : uint8_t
    {
        None,
        Committed,
        Cancelled
    };

    const char* KeyLabel(int index, char* single)
    {
        if (index < CHAR_KEY_COUNT)
        {
            single[0] = CHAR_KEYS[index];
            single[1] = '\0';
            return single;
        }
        if (index == KEY_SPACE)
            return "SPC";
        if (index == KEY_BACKSPACE)
            return "DEL";
        if (index == KEY_DONE)
            return "OK";
        return "X";
    }

    void AppendChar(char* buffer, size_t size, char c)
    {
        const size_t length = strlen(buffer);
        if (length + 1 < size)
        {
            buffer[length] = c;
            buffer[length + 1] = '\0';
        }
    }

    void Backspace(char* buffer)
    {
        const size_t length = strlen(buffer);
        if (length > 0)
            buffer[length - 1] = '\0';
    }

    void CopyBounded(char* dst, size_t dstSize, const char* src)
    {
        if (dstSize == 0)
            return;
        if (!src)
        {
            dst[0] = '\0';
            return;
        }
        strncpy(dst, src, dstSize - 1);
        dst[dstSize - 1] = '\0';
    }

    /// Bounded by UI_TEXT_INPUT_MAX regardless of the caller's own buffer size,
    /// because that is the most this API ever holds open for a cancel to
    /// restore -- the same reasoning UI_TEXT_MAX already carries for display.
    void TakeSnapshot(char* snapshot, const char* buffer, size_t size)
    {
        const size_t n = (size < UI_TEXT_INPUT_MAX) ? size : static_cast<size_t>(UI_TEXT_INPUT_MAX);
        CopyBounded(snapshot, n, buffer);
    }

    /// Draws the whole grid inside whatever panel is currently open (a real
    /// modal, so the caller must already be one) and applies the activated key
    /// directly to buffer. Left/Right cycle within a row, Up/Down step between
    /// rows -- the ordinary run navigation every same-line group already has.
    KeyAction DrawKeyboardGrid(char* buffer, size_t size)
    {
        const UiStyle& style = Ui_GetStyle();
        const int rowHeight = Ui_TextHeight(style.textScale) + style.rowPadding * 2;
        const int keyWidth = Ui_ContentWidth() / KEYBOARD_COLUMNS;
        if (keyWidth <= 0)
            return KeyAction::None;

        KeyAction action = KeyAction::None;
        for (int row = 0; row < KEYBOARD_ROW_COUNT; ++row)
        {
            int rx = 0;
            int ry = 0;
            int rw = 0;
            if (!UiInternal_TakeRow(rowHeight, &rx, &ry, &rw, nullptr))
                break;

            for (int col = 0; col < KEYBOARD_COLUMNS; ++col)
            {
                const int index = row * KEYBOARD_COLUMNS + col;
                Ui_SameLine(keyWidth);

                char keyName[8];
                snprintf(keyName, sizeof(keyName), "key%d", index);
                const uint32_t keyId = UiInternal_Id(keyName);

                int kx = 0;
                int ky = 0;
                int kw = 0;
                const bool activated = UiInternal_ActivatableRow(keyId, false, &kx, &ky, &kw);
                if (kw > 0)
                {
                    char single[2];
                    const char* label = KeyLabel(index, single);
                    const int textWidth = Ui_TextWidth(style.textScale, label);
                    UiFont_Draw(kx + (kw - textWidth) / 2, ky + style.rowPadding, style.textScale, label, Ui_GetColor(UiColor::Text));
                }

                if (!activated)
                    continue;

                if (index < CHAR_KEY_COUNT)
                    AppendChar(buffer, size, CHAR_KEYS[index]);
                else if (index == KEY_SPACE)
                    AppendChar(buffer, size, ' ');
                else if (index == KEY_BACKSPACE)
                    Backspace(buffer);
                else if (index == KEY_DONE)
                    action = KeyAction::Committed;
                else
                    action = KeyAction::Cancelled;
            }
        }

        if (action == KeyAction::None && Ui_WasBackPressed())
            action = KeyAction::Cancelled;
        return action;
    }

    /// The field's name and current value, with a solid caret after it --
    /// vertex and colour only, the same reason every other solid fill in this
    /// interface stopped sampling the font atlas.
    void DrawValueRow(const char* title, const char* buffer)
    {
        const UiStyle& style = Ui_GetStyle();
        int rx = 0;
        int ry = 0;
        int rw = 0;
        if (!UiInternal_TakeRow(Ui_TextHeight(style.textScale) + style.rowPadding * 2, &rx, &ry, &rw, nullptr))
            return;

        UiFont_Draw(rx + style.rowPadding, ry + style.rowPadding, style.textScale, title, Ui_GetColor(UiColor::TextDim));
        const int titleWidth = Ui_TextWidth(style.textScale, title);
        const int valueX = rx + style.rowPadding + titleWidth + style.itemSpacing;
        UiFont_Draw(valueX, ry + style.rowPadding, style.textScale, buffer, Ui_GetColor(UiColor::Text));
        const int caretX = valueX + Ui_TextWidth(style.textScale, buffer);
        if (caretX < rx + rw - style.rowPadding)
            UiInternal_PushRect(caretX, ry + style.rowPadding, style.caretWidth, Ui_TextHeight(style.textScale), Ui_GetColor(UiColor::TextAccent));
    }

    /// Drains PlatformCapability::TextCharacters and applies it to buffer
    /// directly: 0x08 backspaces, 0x0D commits, 0x1B cancels, everything else
    /// printable is appended if there is room. Deaf to Ui_WasBackPressed on
    /// purpose -- Win32 bridges its own Backspace key to the same gamepad Back
    /// signal a modal's Cancel button reads, so a widget in this mode must take
    /// its cancel from the byte stream alone or the same keystroke would both
    /// delete a character and close the field.
    CharAction DrainCharacters(char* buffer, size_t size)
    {
        Platform* platform = Engine_GetPlatform();
        if (!platform || size == 0)
            return CharAction::None;

        char popped[16];
        const uint32_t count = platform->Keyboard_PopCharacters(popped, sizeof(popped));
        CharAction result = CharAction::None;
        for (uint32_t i = 0; i < count; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(popped[i]);
            if (c == 8)
                Backspace(buffer);
            else if (c == 13)
                result = CharAction::Committed;
            else if (c == 27)
                result = CharAction::Cancelled;
            else if (c >= 32 && c < 127)
                AppendChar(buffer, size, static_cast<char>(c));
        }
        return result;
    }

    int ModalWidth() { return (Ui_ScreenWidth() * 9) / 10; }
    int ModalHeight() { return (Ui_ScreenHeight() * 8) / 10; }

    // --- Ui_MessageDialog / Ui_ConfirmDialog / Ui_TextDialog ---------------
    //
    // Exactly one of these may be open at once, matching Vita's own dialog
    // service and Win32's blocking MessageBox alike: opening a second while
    // one is already open abandons it rather than queuing.

    uint32_t s_SessionId = 0;
    bool s_SessionUsesPlatform = false;
    bool s_SessionUsesChars = false;
    char s_SessionTitle[UI_TEXT_MAX];
    char s_SessionBody[UI_TEXT_MAX];
    char s_SessionSnapshot[UI_TEXT_INPUT_MAX];

    UiDialogResult DialogSessionUpdate(uint32_t id, DialogKind kind, const char* title, const char* body, char* buffer, size_t size)
    {
        if (!UiInternal_CanDraw())
            return UiDialogResult::None;

        Platform* platform = Engine_GetPlatform();

        if (s_SessionId != id)
        {
            if (s_SessionId != 0 && s_SessionUsesPlatform && platform)
                platform->Dialog_Cancel();

            s_SessionId = id;
            CopyBounded(s_SessionTitle, sizeof(s_SessionTitle), title);
            CopyBounded(s_SessionBody, sizeof(s_SessionBody), body);
            if (kind == DialogKind::TextInput && buffer)
                TakeSnapshot(s_SessionSnapshot, buffer, size);

            s_SessionUsesPlatform = false;
            s_SessionUsesChars = false;

            const bool hasDialog = platform && platform->HasCapability(PlatformCapability::SystemDialog);
            if (hasDialog)
            {
                DialogRequest request;
                request.kind = kind;
                request.title = s_SessionTitle;
                request.body = (kind == DialogKind::TextInput) ? nullptr : s_SessionBody;
                request.textBuffer = (kind == DialogKind::TextInput) ? buffer : nullptr;
                request.textBufferSize = (kind == DialogKind::TextInput) ? size : 0;
                s_SessionUsesPlatform = platform->Dialog_Open(request);
            }
            if (!s_SessionUsesPlatform && kind == DialogKind::TextInput)
                s_SessionUsesChars = platform && platform->HasCapability(PlatformCapability::TextCharacters);
        }

        if (s_SessionUsesPlatform)
        {
            const DialogStatus status = platform->Dialog_Poll();
            if (status == DialogStatus::Pending)
                return UiDialogResult::Pending;
            s_SessionId = 0;
            return (status == DialogStatus::Accepted) ? UiDialogResult::Accepted : UiDialogResult::Cancelled;
        }

        // The drawn path is a real modal, so the caller must already be
        // calling this outside any panel -- the same placement Ui_BeginModal
        // itself requires.
        if (!Ui_BeginModal(s_SessionTitle, ModalWidth(), ModalHeight()))
            return UiDialogResult::Pending;

        UiDialogResult result = UiDialogResult::Pending;

        if (kind == DialogKind::TextInput)
        {
            DrawValueRow(s_SessionTitle, buffer);
            Ui_Spacing(Ui_GetStyle().itemSpacing);

            if (s_SessionUsesChars)
            {
                const CharAction action = DrainCharacters(buffer, size);
                if (action == CharAction::Committed)
                    result = UiDialogResult::Accepted;
                else if (action == CharAction::Cancelled)
                    result = UiDialogResult::Cancelled;
            }
            else
            {
                const KeyAction action = DrawKeyboardGrid(buffer, size);
                if (action == KeyAction::Committed)
                    result = UiDialogResult::Accepted;
                else if (action == KeyAction::Cancelled)
                    result = UiDialogResult::Cancelled;
            }
        }
        else
        {
            Ui_LabelWrapped(s_SessionBody, UiColor::Text);
            Ui_Spacing(Ui_GetStyle().itemSpacing);

            if (kind == DialogKind::Confirm)
            {
                Ui_SameLine(Ui_ContentWidth() / 2 - Ui_GetStyle().itemSpacing / 2);
                if (Ui_Button("OK"))
                    result = UiDialogResult::Accepted;
                Ui_SameLine(0);
                if (Ui_Button("CANCEL"))
                    result = UiDialogResult::Cancelled;
            }
            else if (Ui_Button("OK"))
            {
                result = UiDialogResult::Accepted;
            }

            if (result == UiDialogResult::Pending && Ui_WasBackPressed())
                result = (kind == DialogKind::Confirm) ? UiDialogResult::Cancelled : UiDialogResult::Accepted;
        }

        if (result == UiDialogResult::Cancelled && kind == DialogKind::TextInput && buffer)
            CopyBounded(buffer, size, s_SessionSnapshot);

        Ui_EndModal();

        if (result != UiDialogResult::Pending)
            s_SessionId = 0;
        return result;
    }

    // --- Ui_TextInput (inline row) -----------------------------------------
    //
    // At most one field edits at once, the same restriction the dialog family
    // above already has, tracked separately because a row's identity comes
    // from its label rather than a caller-supplied id.

    uint32_t s_RowEditId = 0;
    bool s_RowUsesPlatform = false;
    bool s_RowUsesChars = false;
    bool s_RowUsesGrid = false;
    char* s_RowBuffer = nullptr;
    size_t s_RowBufferSize = 0;
    char s_RowLabel[UI_TEXT_MAX];
    char s_RowSnapshot[UI_TEXT_INPUT_MAX];

    // Read once by the very next Ui_TextInput call for this id: the grid
    // mechanism commits from UiInternal_DrawTextEditOverlay, after this row's
    // own call for the frame has already returned, so "the buffer changed"
    // can only be reported to the call that follows.
    uint32_t s_RowJustChangedId = 0;

    void EndRowEdit()
    {
        s_RowEditId = 0;
        s_RowUsesPlatform = false;
        s_RowUsesChars = false;
        s_RowUsesGrid = false;
        s_RowBuffer = nullptr;
        s_RowBufferSize = 0;
    }
} // namespace

void UiInternal_DialogReset()
{
    Platform* platform = Engine_GetPlatform();
    if ((s_SessionId != 0 && s_SessionUsesPlatform) || (s_RowEditId != 0 && s_RowUsesPlatform))
    {
        if (platform)
            platform->Dialog_Cancel();
    }
    s_SessionId = 0;
    s_SessionUsesPlatform = false;
    s_SessionUsesChars = false;
    EndRowEdit();
    s_RowJustChangedId = 0;
}

UiDialogResult Ui_MessageDialog(const char* id, const char* title, const char* body)
{
    if (!id)
        return UiDialogResult::None;
    return DialogSessionUpdate(UiInternal_Id(id), DialogKind::Message, title, body, nullptr, 0);
}

UiDialogResult Ui_ConfirmDialog(const char* id, const char* title, const char* body)
{
    if (!id)
        return UiDialogResult::None;
    return DialogSessionUpdate(UiInternal_Id(id), DialogKind::Confirm, title, body, nullptr, 0);
}

UiDialogResult Ui_TextDialog(const char* id, const char* title, char* buffer, size_t size)
{
    if (!id || !buffer || size == 0)
        return UiDialogResult::None;
    return DialogSessionUpdate(UiInternal_Id(id), DialogKind::TextInput, title, nullptr, buffer, size);
}

bool Ui_TextInput(const char* label, char* buffer, size_t size)
{
    if (!UiInternal_CanDraw() || !label || !buffer || size == 0)
        return false;

    const uint32_t id = UiInternal_Id(label);
    const UiStyle& style = Ui_GetStyle();
    UiFrameState& state = UiInternal_State();
    const bool editingThis = (s_RowEditId == id);

    bool changed = false;
    if (s_RowJustChangedId == id)
    {
        changed = true;
        s_RowJustChangedId = 0;
    }

    int x = 0;
    int y = 0;
    int w = 0;
    const bool activated = UiInternal_ActivatableRow(id, editingThis, &x, &y, &w);
    if (w == 0)
        return changed;

    UiFont_Draw(x + style.rowPadding, y + style.rowPadding, style.textScale, label, Ui_GetColor(UiInternal_TextRole(UiColor::Text)));
    const int labelWidth = Ui_TextWidth(style.textScale, label);
    const int valueX = x + style.rowPadding + labelWidth + style.itemSpacing;
    const int valueMaxW = (x + w - style.rowPadding) - valueX;

    if (editingThis)
    {
        // Suppresses the ordinary focus-resolution pass in Ui_EndFrame
        // entirely (see UiFrameState::textEditCapturing), the same way an
        // open menu already suppresses it -- Up/Down must not move focus off
        // this row mid-edit.
        state.textEditCapturing = true;

        if (s_RowUsesPlatform)
        {
            Platform* platform = Engine_GetPlatform();
            const DialogStatus status = platform ? platform->Dialog_Poll() : DialogStatus::Idle;
            if (status == DialogStatus::Accepted)
            {
                changed = changed || (strcmp(buffer, s_RowSnapshot) != 0);
                EndRowEdit();
            }
            else if (status == DialogStatus::Cancelled)
            {
                EndRowEdit();
            }
        }
        else if (s_RowUsesChars)
        {
            const CharAction action = DrainCharacters(buffer, size);
            if (action == CharAction::Committed)
            {
                changed = changed || (strcmp(buffer, s_RowSnapshot) != 0);
                EndRowEdit();
            }
            else if (action == CharAction::Cancelled)
            {
                CopyBounded(buffer, size, s_RowSnapshot);
                EndRowEdit();
            }
        }
        // s_RowUsesGrid is driven from UiInternal_DrawTextEditOverlay once
        // this panel has closed -- Ui_BeginModal refuses while one is already
        // open, which this row still is.

        const char* tail = Ui_TextFitTail(style.textScale, buffer, valueMaxW);
        UiFont_Draw(valueX, y + style.rowPadding, style.textScale, tail, Ui_GetColor(UiColor::TextAccent));
        const int caretX = valueX + Ui_TextWidth(style.textScale, tail);
        if (caretX < x + w - style.rowPadding)
            UiInternal_PushRect(caretX, y + style.rowPadding, style.caretWidth, Ui_TextHeight(style.textScale), Ui_GetColor(UiColor::TextAccent));
    }
    else
    {
        const char* tail = Ui_TextFitTail(style.textScale, buffer, valueMaxW);
        UiFont_Draw(valueX, y + style.rowPadding, style.textScale, tail, Ui_GetColor(UiInternal_TextRole(UiColor::TextDim)));

        if (activated && s_RowEditId == 0)
        {
            TakeSnapshot(s_RowSnapshot, buffer, size);
            CopyBounded(s_RowLabel, sizeof(s_RowLabel), label);
            s_RowEditId = id;
            s_RowBuffer = buffer;
            s_RowBufferSize = size;
            s_RowUsesPlatform = false;
            s_RowUsesChars = false;
            s_RowUsesGrid = false;

            Platform* platform = Engine_GetPlatform();
            const bool hasDialog = platform && platform->HasCapability(PlatformCapability::SystemDialog);
            if (hasDialog)
            {
                DialogRequest request;
                request.kind = DialogKind::TextInput;
                request.title = s_RowLabel;
                request.body = nullptr;
                request.textBuffer = buffer;
                request.textBufferSize = size;
                s_RowUsesPlatform = platform->Dialog_Open(request);
            }
            if (!s_RowUsesPlatform)
            {
                const bool hasChars = platform && platform->HasCapability(PlatformCapability::TextCharacters);
                if (hasChars)
                    s_RowUsesChars = true;
                else
                    s_RowUsesGrid = true;
            }
        }
    }

    return changed;
}

void UiInternal_DrawTextEditOverlay()
{
    if (s_RowEditId == 0 || !s_RowUsesGrid || !s_RowBuffer)
        return;

    if (!Ui_BeginModal(s_RowLabel, ModalWidth(), ModalHeight()))
        return; // the caller's own panel is still open; try again next frame

    DrawValueRow(s_RowLabel, s_RowBuffer);
    Ui_Spacing(Ui_GetStyle().itemSpacing);
    const KeyAction action = DrawKeyboardGrid(s_RowBuffer, s_RowBufferSize);

    if (action == KeyAction::Cancelled)
    {
        CopyBounded(s_RowBuffer, s_RowBufferSize, s_RowSnapshot);
        EndRowEdit();
    }
    else if (action == KeyAction::Committed)
    {
        if (strcmp(s_RowBuffer, s_RowSnapshot) != 0)
            s_RowJustChangedId = s_RowEditId;
        EndRowEdit();
    }

    Ui_EndModal();
}
