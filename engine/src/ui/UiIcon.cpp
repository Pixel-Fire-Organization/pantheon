#include "../../include/ui/EngineUi.h"

#include "UiInternal.h"
#include "platform/Platform.h"

namespace
{
    /// Short stand-ins, one per real UiIcon value in declaration order, drawn
    /// as ordinary text when the active font has no cell for an icon -- no
    /// cooked font at all, or an atlas that predates a given one. A hint bar
    /// therefore never goes silent for a button it cannot draw a picture of.
    const char* const FALLBACK[static_cast<uint8_t>(UiIcon::Count) - 1] = {
        "X", "O", "[]", "/\\",  // ButtonSouth, ButtonEast, ButtonWest, ButtonNorth
        "A", "B", "X", "Y",     // ButtonA, ButtonB, ButtonX, ButtonY
        "L1", "R1", "L2", "R2", "+", "(L", "R)",
        "OK", "X", "!", "i", "DIR", "DOC", "v", ">",
    };

    /// @return The cell index a UiIcon addresses; only valid for a real icon.
    uint8_t CellIndex(UiIcon icon) { return static_cast<uint8_t>(icon) - 1; }

    const char* FallbackText(UiIcon icon)
    {
        const uint8_t index = CellIndex(icon);
        return (index < (sizeof(FALLBACK) / sizeof(FALLBACK[0]))) ? FALLBACK[index] : "?";
    }

    const FontGlyph* ResolveCell(UiIcon icon, const Font** outFont)
    {
        *outFont = nullptr;
        if (icon == UiIcon::None || icon >= UiIcon::Count)
            return nullptr;
        const Font* font = UiInternal_CookedFont();
        if (!font)
            return nullptr;
        const FontGlyph* cell = Font_GetCell(font, CellIndex(icon));
        if (!cell || cell->w == 0 || cell->h == 0)
            return nullptr;
        *outFont = font;
        return cell;
    }
} // namespace

int Ui_IconAt(int x, int y, int scale, UiIcon icon, UiColor role)
{
    if (icon == UiIcon::None || scale <= 0)
        return x;

    const UiRgba color = Ui_GetColor(role);
    const Font* font = nullptr;
    const FontGlyph* cell = ResolveCell(icon, &font);
    if (cell)
    {
        const uint32_t aw = font->atlasWidth;
        const uint32_t ah = font->atlasHeight;
        const uint16_t u0 = static_cast<uint16_t>(static_cast<uint32_t>(cell->u) * 65535u / aw);
        const uint16_t v0 = static_cast<uint16_t>(static_cast<uint32_t>(cell->v) * 65535u / ah);
        const uint16_t u1 = static_cast<uint16_t>(static_cast<uint32_t>(cell->u + cell->w) * 65535u / aw);
        const uint16_t v1 = static_cast<uint16_t>(static_cast<uint32_t>(cell->v + cell->h) * 65535u / ah);
        UiInternal_PushTexturedQuad(x + cell->bearingX * scale, y + cell->bearingY * scale, cell->w * scale, cell->h * scale, UiInternal_AtlasTexture(), u0, v0, u1, v1, color);
        return x + cell->advance * scale;
    }

    return UiFont_Draw(x, y, scale, FallbackText(icon), color);
}

void Ui_Icon(UiIcon icon, UiColor role)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    bool visible = false;
    if (!UiInternal_CanDraw() || !UiInternal_TakeRow(Ui_TextHeight(style.textScale), &x, &y, &w, &visible) || !visible)
        return;
    Ui_IconAt(x, y, style.textScale, icon, role);
}

bool Ui_IconExists(UiIcon icon)
{
    const Font* font = nullptr;
    return ResolveCell(icon, &font) != nullptr;
}

bool Ui_IconButton(const char* label, UiIcon icon)
{
    const UiStyle& style = Ui_GetStyle();
    int x = 0;
    int y = 0;
    int w = 0;
    const bool activated = UiInternal_ActivatableRow(UiInternal_Id(label), false, &x, &y, &w);
    if (w == 0)
        return false;

    const UiColor role = UiInternal_TextRole(UiColor::Text);
    int contentWidth = Ui_TextWidth(style.textScale, label);
    if (icon != UiIcon::None)
        contentWidth += style.iconSpacing + Ui_TextHeight(style.textScale);

    int penX = x + (w - contentWidth) / 2;
    const int penY = y + style.rowPadding;
    if (icon != UiIcon::None)
    {
        penX = Ui_IconAt(penX, penY, style.textScale, icon, role);
        penX += style.iconSpacing;
    }
    UiFont_Draw(penX, penY, style.textScale, label, Ui_GetColor(role));

    return activated;
}

UiIcon Ui_ButtonIcon(GamepadButton button)
{
    const Platform* platform = Engine_GetPlatform();
    const UiButtonIconFamily family = platform ? static_cast<UiButtonIconFamily>(platform->GetConstant(PlatformConstant::ButtonIconFamily)) : UiButtonIconFamily::PlayStation;
    const bool xbox = (family == UiButtonIconFamily::Xbox);
    const bool nintendo = (family == UiButtonIconFamily::Nintendo);

    switch (button)
    {
    case GamepadButton::Cross:
        return (xbox || nintendo) ? UiIcon::ButtonA : UiIcon::ButtonSouth;
    case GamepadButton::Circle:
        return (xbox || nintendo) ? UiIcon::ButtonB : UiIcon::ButtonEast;
    case GamepadButton::Square:
        return xbox ? UiIcon::ButtonX : (nintendo ? UiIcon::ButtonY : UiIcon::ButtonWest);
    case GamepadButton::Triangle:
        return xbox ? UiIcon::ButtonY : (nintendo ? UiIcon::ButtonX : UiIcon::ButtonNorth);
    case GamepadButton::L1:
        return UiIcon::L1;
    case GamepadButton::R1:
        return UiIcon::R1;
    case GamepadButton::L2:
        return UiIcon::L2;
    case GamepadButton::R2:
        return UiIcon::R2;
    case GamepadButton::L3:
        return UiIcon::StickLeft;
    case GamepadButton::R3:
        return UiIcon::StickRight;
    case GamepadButton::DPadUp:
    case GamepadButton::DPadDown:
    case GamepadButton::DPadLeft:
    case GamepadButton::DPadRight:
        return UiIcon::DPad;
    case GamepadButton::Unknown:
    case GamepadButton::Select:
    case GamepadButton::Start:
        break;
    }
    return UiIcon::None;
}

void Ui_HintBar(const UiHint* hints, int count)
{
    if (!hints || count <= 0)
        return;

    const UiStyle& style = Ui_GetStyle();
    const int scale = style.textScale;
    const int height = Ui_TextHeight(scale) + style.rowPadding * 2;

    int width = 0;
    for (int i = 0; i < count; ++i)
    {
        if (hints[i].icon != UiIcon::None)
            width += Ui_TextHeight(scale) + style.iconSpacing;
        if (hints[i].text && hints[i].text[0])
            width += Ui_TextWidth(scale, hints[i].text);
        if (i + 1 < count)
            width += style.itemSpacing * 4;
    }

    const int x = (Ui_ScreenWidth() - width) / 2;
    const int y = Ui_ScreenHeight() - height - style.screenMargin;

    Ui_BeginOverlay();
    int penX = x;
    for (int i = 0; i < count; ++i)
    {
        if (hints[i].icon != UiIcon::None)
        {
            penX = Ui_IconAt(penX, y + style.rowPadding, scale, hints[i].icon, UiColor::TextAccent);
            penX += style.iconSpacing;
        }
        if (hints[i].text && hints[i].text[0])
            penX = UiFont_Draw(penX, y + style.rowPadding, scale, hints[i].text, Ui_GetColor(UiColor::Text));
        penX += style.itemSpacing * 4;
    }
    Ui_EndOverlay();
}
