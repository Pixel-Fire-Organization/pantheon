#include "../../include/ui/EngineUi.h"

#include "PlatformConstants.h"

namespace
{
    /// Bring one authored metric onto this display, never below a pixel.
    /// @param value The metric as the theme declared it.
    /// @return The scaled metric.
    int16_t ScaleMetric(int16_t value)
    {
        if (value <= 0)
            return value;
        const int scaled = (static_cast<int>(value) * UI_METRIC_SCALE_PERCENT) / 100;
        return static_cast<int16_t>(scaled > 0 ? scaled : 1);
    }

    /// Bring a whole style onto this display.
    ///
    /// The theme declaration is authored against the console reference
    /// framebuffer, so a platform with a materially smaller screen would
    /// otherwise draw an interface too large to use. Applied once, here, rather
    /// than by each widget: every style reaches the interface through
    /// Ui_SetStyle, built-in and cooked alike, so this is the one place that
    /// cannot be forgotten. Durations are left alone -- they are time, not
    /// space -- and so are colours.
    /// @param style The style as authored.
    /// @return The style as this display should draw it.
    UiStyle ScaleToDisplay(const UiStyle& style)
    {
        UiStyle out = style;
        if (UI_METRIC_SCALE_PERCENT == 100)
            return out;

        out.panelPadding = ScaleMetric(style.panelPadding);
        out.itemSpacing = ScaleMetric(style.itemSpacing);
        out.borderWidth = ScaleMetric(style.borderWidth);
        out.textScale = ScaleMetric(style.textScale);
        out.rowPadding = ScaleMetric(style.rowPadding);
        out.barHeight = ScaleMetric(style.barHeight);
        out.cursorSize = ScaleMetric(style.cursorSize);
        out.screenMargin = ScaleMetric(style.screenMargin);
        out.panelGap = ScaleMetric(style.panelGap);
        out.scrollBarWidth = ScaleMetric(style.scrollBarWidth);
        out.menuBarHeight = ScaleMetric(style.menuBarHeight);
        out.caretWidth = ScaleMetric(style.caretWidth);
        out.iconSpacing = ScaleMetric(style.iconSpacing);
        return out;
    }

    UiStyle s_Style = ScaleToDisplay(Ui_BuiltinTheme(UiBuiltinTheme::MIDNIGHT));

    const char* const s_ColorNames[static_cast<uint8_t>(UiColor::Count)] = {
        "WINDOW BG", "PANEL BG",   "BORDER",      "HEADER", "TEXT",      "TEXT DIM", "TEXT ACCENT", "TEXT WARN", "TEXT DISABLED",
        "ITEM BG",   "ITEM HOVER", "ITEM ACTIVE", "FOCUS",  "BAR TRACK", "BAR FILL", "BAR WARN",    "CURSOR",    "CURSOR EDGE",
    };
} // namespace

UiStyle Ui_DefaultStyle() { return Ui_BuiltinTheme(UiBuiltinTheme::MIDNIGHT); }

const UiStyle& Ui_GetStyle() { return s_Style; }

void Ui_SetStyle(const UiStyle& style) { s_Style = ScaleToDisplay(style); }

UiRgba Ui_GetColor(UiColor role)
{
    const uint8_t index = static_cast<uint8_t>(role);
    if (index >= static_cast<uint8_t>(UiColor::Count))
        return s_Style.colors[static_cast<uint8_t>(UiColor::Text)];
    return s_Style.colors[index];
}

void Ui_SetColor(UiColor role, UiRgba value)
{
    const uint8_t index = static_cast<uint8_t>(role);
    if (index < static_cast<uint8_t>(UiColor::Count))
        s_Style.colors[index] = value;
}

const char* Ui_ColorName(UiColor role)
{
    const uint8_t index = static_cast<uint8_t>(role);
    return (index < static_cast<uint8_t>(UiColor::Count)) ? s_ColorNames[index] : "?";
}
