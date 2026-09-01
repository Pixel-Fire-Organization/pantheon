#include "../../include/ui/EngineUi.h"

#include <cstring>

#include "Engine.h"
#include "UiInternal.h"

namespace
{
    const int TOAST_MARGIN_RATIO = 32;
    const int MODAL_BACKDROP_ALPHA = 168;

    struct Toast
    {
        char text[UI_TEXT_MAX];
        float remaining;
    };

    Toast s_Toasts[UI_MAX_TOASTS];
    uint32_t s_ToastCount = 0;

    int s_ModalSavedClip = 0;
} // namespace

void Ui_BeginOverlay()
{
    if (!UiInternal_CanDraw())
        return;
    UiInternal_State().inOverlay = true;
}

void Ui_EndOverlay()
{
    if (!UiInternal_CanDraw())
        return;
    UiInternal_State().inOverlay = false;
}

bool Ui_BeginModal(const char* title, int w, int h)
{
    if (!UiInternal_CanDraw())
        return false;

    UiFrameState& state = UiInternal_State();
    if (state.modalOpen || state.inPanel)
        return false;

    Ui_BeginOverlay();

    // A dimmed backdrop, which is the first thing per-quad transparency bought.
    UiRgba dim = Ui_GetColor(UiColor::WindowBackground);
    dim.a = MODAL_BACKDROP_ALPHA;
    UiInternal_PushRect(0, 0, Ui_ScreenWidth(), Ui_ScreenHeight(), dim);

    const int x = (Ui_ScreenWidth() - w) / 2;
    const int y = (Ui_ScreenHeight() - h) / 2;
    state.modalOpen = true;
    s_ModalSavedClip = 0;

    Ui_BeginPanel(title, x, y, w, h);
    return true;
}

void Ui_EndModal()
{
    if (!UiInternal_CanDraw())
        return;

    UiFrameState& state = UiInternal_State();
    if (!state.modalOpen)
        return;

    Ui_EndPanel();
    state.modalOpen = false;
    Ui_EndOverlay();
}

void Ui_Toast(const char* text, float seconds)
{
    if (!UiInternal_CanDraw() || !text || seconds <= 0.0f)
        return;

    if (s_ToastCount >= UI_MAX_TOASTS)
    {
        // Bounded and lossy by design: a queue that grows would let a burst of
        // notifications outlive the thing that caused them.
        memmove(&s_Toasts[0], &s_Toasts[1], sizeof(Toast) * (UI_MAX_TOASTS - 1));
        s_ToastCount = UI_MAX_TOASTS - 1;
    }

    Toast& toast = s_Toasts[s_ToastCount++];
    strncpy(toast.text, text, sizeof(toast.text) - 1);
    toast.text[sizeof(toast.text) - 1] = '\0';
    toast.remaining = seconds;
}

void UiInternal_DrawToasts(float dt)
{
    if (!UiInternal_CanDraw() || s_ToastCount == 0)
        return;

    Toast& front = s_Toasts[0];
    front.remaining -= dt;
    if (front.remaining <= 0.0f)
    {
        memmove(&s_Toasts[0], &s_Toasts[1], sizeof(Toast) * (UI_MAX_TOASTS - 1));
        --s_ToastCount;
        if (s_ToastCount == 0)
            return;
    }

    const UiStyle& style = Ui_GetStyle();
    const int scale = style.textScale;
    const int margin = Ui_ScreenWidth() / TOAST_MARGIN_RATIO;
    const int width = Ui_TextWidth(scale, s_Toasts[0].text) + style.panelPadding * 2;
    const int height = Ui_TextHeight(scale) + style.panelPadding * 2;
    const int x = Ui_ScreenWidth() - width - margin;
    const int y = margin;

    Ui_BeginOverlay();
    UiInternal_PushRect(x, y, width, height, Ui_GetColor(UiColor::PanelBackground));
    UiInternal_PushBorder(x, y, width, height, style.borderWidth, Ui_GetColor(UiColor::Border));
    UiFont_Draw(x + style.panelPadding, y + style.panelPadding, scale, s_Toasts[0].text, Ui_GetColor(UiColor::TextAccent));
    Ui_EndOverlay();
}

void UiInternal_ToastsReset()
{
    memset(s_Toasts, 0, sizeof(s_Toasts));
    s_ToastCount = 0;
}

uint32_t Ui_ToastsPending() { return s_ToastCount; }
