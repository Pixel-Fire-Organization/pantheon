#include "../../include/ui/EngineUi.h"

#include "../../include/core/EngineDebug.h"
#include "UiInternal.h"

namespace
{
    // A safety cap on column count, not a budget: nothing about it needs to
    // vary by platform, unlike UI_MAX_CLIP_DEPTH and its family.
    const int MAX_TABLE_COLUMNS = 8;

    bool s_TableOpen = false;
    int s_ColumnWidths[MAX_TABLE_COLUMNS];
    int s_ColumnCount = 0;

    bool s_RowOpen = false;
    int s_RowX = 0;
    int s_RowY = 0;
    int s_RowHeight = 0;
    int s_Column = 0;

    int RowHeight()
    {
        const UiStyle& style = Ui_GetStyle();
        return Ui_TextHeight(style.textScale) + style.rowPadding * 2;
    }

    int ColumnX(int index)
    {
        const UiStyle& style = Ui_GetStyle();
        int x = s_RowX;
        for (int i = 0; i < index; ++i)
            x += s_ColumnWidths[i] + style.itemSpacing;
        return x;
    }
} // namespace

bool Ui_BeginTable(const char* id, const int* widths, int count)
{
    if (!UiInternal_CanDraw())
        return false;

    UiFrameState& state = UiInternal_State();
    if (!state.inPanel || s_TableOpen)
        return false;
    if (count <= 0 || count > MAX_TABLE_COLUMNS)
    {
        Engine_LogError("Ui: table '%s' asked for %d columns, at most %d fit.", id ? id : "", count, MAX_TABLE_COLUMNS);
        return false;
    }

    const UiStyle& style = Ui_GetStyle();
    const int gap = style.itemSpacing;

    int explicitSum = 0;
    int autoCount = 0;
    for (int i = 0; i < count; ++i)
    {
        if (widths && widths[i] > 0)
            explicitSum += widths[i];
        else
            ++autoCount;
    }

    const int available = state.contentW - gap * (count - 1);
    const int remaining = available - explicitSum;
    const int autoWidth = (autoCount > 0 && remaining > 0) ? (remaining / autoCount) : 0;

    for (int i = 0; i < count; ++i)
        s_ColumnWidths[i] = (widths && widths[i] > 0) ? widths[i] : autoWidth;

    s_ColumnCount = count;
    s_TableOpen = true;
    s_RowOpen = false;
    UiInternal_PushId(id);
    return true;
}

void Ui_TableHeader(const char* const* labels)
{
    if (!UiInternal_CanDraw() || !s_TableOpen || !labels)
        return;

    const UiStyle& style = Ui_GetStyle();
    const int height = RowHeight();
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_TakeRow(height, &x, &y, &w, &visible) || !visible)
        return;

    s_RowX = x;
    for (int i = 0; i < s_ColumnCount; ++i)
    {
        if (labels[i])
            UiFont_Draw(ColumnX(i) + style.rowPadding, y + style.rowPadding, style.textScale, labels[i], Ui_GetColor(UiColor::Header));
    }
    UiInternal_PushRect(x, y + height - style.borderWidth, w, style.borderWidth, Ui_GetColor(UiColor::Border));
}

bool Ui_TableRow(const char* id, bool selected)
{
    if (!UiInternal_CanDraw() || !s_TableOpen)
        return false;

    int x = 0;
    int y = 0;
    int w = 0;
    const bool activated = UiInternal_ActivatableRow(UiInternal_Id(id), selected, &x, &y, &w);
    if (w == 0)
        return false;

    s_RowOpen = true;
    s_RowX = x;
    s_RowY = y;
    s_RowHeight = RowHeight();
    s_Column = 0;
    return activated;
}

void Ui_TableCell(const char* text, UiAlign align, UiColor role)
{
    if (!UiInternal_CanDraw() || !s_TableOpen || !s_RowOpen)
        return;
    if (s_Column >= s_ColumnCount)
        return;

    const UiStyle& style = Ui_GetStyle();
    const int columnX = ColumnX(s_Column) + style.rowPadding;
    const int columnW = s_ColumnWidths[s_Column] - style.rowPadding * 2;
    ++s_Column;

    if (!text || columnW <= 0)
        return;

    // The row's own clip already bounds this, but a cell must not overrun its
    // *column* into the next one, so it is fit to the column's own width first.
    const int scale = style.textScale;
    const int fit = Ui_TextFit(scale, text, columnW);
    if (fit <= 0)
        return;

    char cut[UI_TEXT_MAX];
    const int length = (fit < UI_TEXT_MAX - 1) ? fit : (UI_TEXT_MAX - 1);
    for (int i = 0; i < length; ++i)
        cut[i] = text[i];
    cut[length] = '\0';

    Ui_TextAligned(columnX, s_RowY + style.rowPadding, columnW, scale, cut, align, UiInternal_TextRole(role));
}

void Ui_EndTable()
{
    if (!UiInternal_CanDraw() || !s_TableOpen)
        return;
    UiInternal_PopId();
    s_TableOpen = false;
    s_RowOpen = false;
}
