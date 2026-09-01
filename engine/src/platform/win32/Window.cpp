#include <cstring>
#include "core/EngineDebug.h"

#include "Macros.h"
#include "Platform.h"

#include <windows.h>

namespace
{
    const char* const kWindowClass = "Ps2EngineWindow";

    // The WndProc is a plain function, so it needs a way back to the platform.
    // Single-window process: a file-scope pointer is honest and avoids stuffing
    // it through GWLP_USERDATA for no benefit.
    Win32WindowState* g_State = nullptr;

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (!g_State)
            return DefWindowProcA(hwnd, msg, wParam, lParam);

        switch (msg)
        {
        case WM_CLOSE:
        case WM_DESTROY:
            g_State->shouldClose = true;
            return 0;

        case WM_SIZE:
            {
                const uint32_t w = static_cast<uint32_t>(LOWORD(lParam));
                const uint32_t h = static_cast<uint32_t>(HIWORD(lParam));
                // A minimised window reports 0x0. Renderers would choke on a
                // zero-sized swapchain, so keep the last real size instead.
                if (w > 0 && h > 0)
                {
                    g_State->width = w;
                    g_State->height = h;
                    g_State->resized = true;
                }
                return 0;
            }

        case WM_MOUSEWHEEL:
            g_State->wheelDelta += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (g_State->logInput)
                Engine_LogInfo("input: WM_KEYDOWN vk=0x%02X", static_cast<unsigned>(wParam));
            if (wParam < 256)
            {
                g_State->keyDown[wParam] = true;
                g_State->keyHit[wParam] = true; // latched, so a same-frame release still registers
            }
            return 0;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (wParam < 256)
                g_State->keyDown[wParam] = false;
            return 0;

        // Printable ASCII, plus the three control bytes Platform.h documents
        // for Keyboard_PopCharacters: this is the character channel behind
        // PlatformCapability::TextCharacters, and its whole contract is that
        // nothing above the platform boundary ever sees a byte it has no
        // meaning for.
        case WM_CHAR:
            if (((wParam >= 32 && wParam < 127) || wParam == 8 || wParam == 13 || wParam == 27) && g_State->charCount < Win32WindowState::CHAR_BUFFER_SIZE)
                g_State->charBuffer[g_State->charCount++] = static_cast<char>(wParam);
            return 0;

        // Losing focus would otherwise leave keys stuck down forever: the
        // matching WM_KEYUP is delivered to whoever took focus, not to us.
        case WM_KILLFOCUS:
            memset(g_State->keyDown, 0, sizeof(g_State->keyDown));
            memset(g_State->mouseDown, 0, sizeof(g_State->mouseDown));
            return 0;

        case WM_LBUTTONDOWN:
            g_State->mouseDown[0] = true;
            g_State->mouseHit[0] = true;
            return 0;
        case WM_LBUTTONUP:
            g_State->mouseDown[0] = false;
            return 0;
        case WM_RBUTTONDOWN:
            g_State->mouseDown[1] = true;
            g_State->mouseHit[1] = true;
            return 0;
        case WM_RBUTTONUP:
            g_State->mouseDown[1] = false;
            return 0;
        case WM_MBUTTONDOWN:
            g_State->mouseDown[2] = true;
            g_State->mouseHit[2] = true;
            return 0;
        case WM_MBUTTONUP:
            g_State->mouseDown[2] = false;
            return 0;

        default:
            break;
        }

        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
} // namespace

bool Win32Platform::WindowOpen(const WindowDesc& desc)
{
    m_window.width = desc.width ? desc.width : GFX_SCREEN_WIDTH;
    m_window.height = desc.height ? desc.height : GFX_SCREEN_HEIGHT;
    m_window.shouldClose = false;
    m_window.resized = false;
    m_window.wheelDelta = 0.0f;
    m_window.charCount = 0;
    g_State = &m_window;

    HINSTANCE instance = GetModuleHandleA(nullptr);

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = &WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExA(&wc))
    {
        Engine_LogError("%s: RegisterClassEx failed (%lu)", GetName(), GetLastError());
        return false;
    }

    // Size the CLIENT area to the requested resolution: AdjustWindowRect grows
    // the outer rect by the border and title bar, so the framebuffer really is
    // the size the renderer was told.
    RECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = static_cast<LONG>(m_window.width);
    rect.bottom = static_cast<LONG>(m_window.height);

    const DWORD style = desc.resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
    AdjustWindowRect(&rect, style, FALSE);

    m_window.hwnd =
        CreateWindowExA(0, kWindowClass, desc.title ? desc.title : "Engine", style, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);

    if (!m_window.hwnd)
    {
        Engine_LogError("%s: CreateWindowEx failed (%lu)", GetName(), GetLastError());
        return false;
    }

    ShowWindow(static_cast<HWND>(m_window.hwnd), SW_SHOW);
    UpdateWindow(static_cast<HWND>(m_window.hwnd));

    // Engine log strings are UTF-8; without this the console decodes them as the
    // OEM codepage and prints mojibake.
    SetConsoleOutputCP(CP_UTF8);

    Engine_LogInfo("%s: window %ux%u opened", GetName(), m_window.width, m_window.height);
    return true;
}

void Win32Platform::PumpMessages()
{
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

void Win32Platform::WindowClose()
{
    if (!m_window.hwnd)
        return;

    DestroyWindow(static_cast<HWND>(m_window.hwnd));
    UnregisterClassA(kWindowClass, GetModuleHandleA(nullptr));
    m_window.hwnd = nullptr;
    g_State = nullptr;
}

bool Win32Platform::WindowShouldClose() const { return m_window.shouldClose; }

void Win32Platform::GetFramebufferSize(uint32_t* outWidth, uint32_t* outHeight) const
{
    if (outWidth)
        *outWidth = m_window.width;
    if (outHeight)
        *outHeight = m_window.height;
}

void* Win32Platform::GetNativeWindowHandle() const { return m_window.hwnd; }

bool Win32Platform::Dialog_Open(const DialogRequest& request)
{
    if (request.kind == DialogKind::TextInput)
    {
        // No host text dialog on this platform -- WM_CHAR already delivers
        // typed characters directly, which is what PlatformCapability::
        // TextCharacters is for, so there is nothing this call would add.
        return false;
    }

    const UINT type = (request.kind == DialogKind::Confirm) ? MB_OKCANCEL : MB_OK;
    const int result = MessageBoxA(static_cast<HWND>(m_window.hwnd), request.body ? request.body : "", request.title ? request.title : "", type);

    // MessageBoxA blocks until dismissed, so the result is already known by
    // the time this returns; Dialog_Poll's non-blocking contract is honoured
    // by handing that known result to the first poll instead.
    m_dialogResult = (result == IDCANCEL) ? DialogStatus::Cancelled : DialogStatus::Accepted;
    return true;
}

DialogStatus Win32Platform::Dialog_Poll()
{
    const DialogStatus result = m_dialogResult;
    if (result == DialogStatus::Accepted || result == DialogStatus::Cancelled)
        m_dialogResult = DialogStatus::Idle;
    return result;
}

void Win32Platform::Dialog_Cancel()
{
    // MessageBoxA has already returned by the time Dialog_Open does; there is
    // nothing left running to cancel, only the result of one still unread.
    m_dialogResult = DialogStatus::Idle;
}

