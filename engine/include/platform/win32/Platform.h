#pragma once

#include "PlatformConstants.h"
#include "platform/Platform.h"

struct Win32WindowState
{
    void* hwnd;
    uint32_t width;
    uint32_t height;
    float wheelDelta;
    bool shouldClose;
    bool resized; // set on WM_SIZE; the renderer clears it after reconfiguring

    // Raw keyboard/mouse state, written by the WndProc and drained by PollInput.
    //
    // `down` is the live held state. `hit` latches a press that arrived since the
    // last poll, so a key tapped and released inside a single frame is still seen
    // - GetAsyncKeyState used to lose exactly those.
    bool logInput; // mirrors --log-input so the WndProc can report raw messages
    bool keyDown[256];
    bool keyHit[256];
    bool mouseDown[8];
    bool mouseHit[8];

    // WM_CHAR, filtered at the WndProc to printable ASCII plus backspace/enter/
    // escape so nothing above this boundary ever sees a byte it has no meaning
    // for. Drained by Keyboard_PopCharacters; a frame that does not drain it
    // keeps what did not fit rather than dropping it.
    enum : uint32_t
    {
        CHAR_BUFFER_SIZE = 64
    };
    char charBuffer[CHAR_BUFFER_SIZE];
    uint32_t charCount;
};

// ---------------------------------------------------------------------------
// Win32Platform - the native Windows target.
//
// Cross-compiled from WSL with MinGW-w64 (toolchains/mingw-w64.cmake), so every
// GCC extension the engine already uses compiles unchanged.
//
// Unlike the PS2 this is a real desktop target: resizable window, arbitrary
// resolution, unconstrained heap, keyboard and mouse as first-class devices.
//
// Method bodies are split by concern across Platform.cpp, Memory.cpp, Time.cpp,
// Thread.cpp, Console.cpp, Filesystem.cpp, Input.cpp and Window.cpp.
// ---------------------------------------------------------------------------
class Win32Platform final : public Platform
{
public:
    Win32Platform();
    ~Win32Platform() override = default;

    Win32Platform(const Win32Platform&) = delete;
    Win32Platform(Win32Platform&&) = delete;
    Win32Platform& operator=(const Win32Platform&) = delete;
    Win32Platform& operator=(Win32Platform&&) = delete;

    PlatformId GetId() const override { return PlatformId::Win32; }
    const char* GetName() const override { return "win32"; }

    bool Init(const StartupArgs& args) override;
    void Shutdown() override;
    const StartupArgs& GetStartupArgs() const override;

    uint32_t GetConstant(PlatformConstant key) const override;
    bool HasCapability(PlatformCapability key) const override;

    /// @param chord Which debug action to query.
    /// @return The full-pad mask; this pad has both shoulder rows and both
    ///         stick clicks.
    uint16_t GetDebugChord(DebugChord chord) const override;

    // --- Memory.cpp ---------------------------------------------------------
    /// @return Null; this platform has no achievements.
    AchievementContract* GetAchievements() override { return nullptr; }

    MemoryContract& GetMemory() override { return m_memory; }
    const MemoryContract& GetMemory() const override { return m_memory; }
    uint32_t GetTextureFootprintBytes(uint32_t width, uint32_t height, PixelFormat format, uint8_t mipCount) const override;

    // --- Filesystem.cpp -----------------------------------------------------
    bool BuildPath(const char* relativePath, char* outBuf, size_t bufSize) const override;
    bool BuildWritablePath(const char* relativePath, char* outBuf, size_t bufSize) const override;
    const char* GetResourceToken() const override { return m_dataRoot; }
    FileHandle FileOpen(const char* path, FileMode mode) override;
    bool FileSeek(FileHandle file, uint64_t offset) override;
    size_t FileRead(FileHandle file, void* dst, size_t bytes) override;
    size_t FileWrite(FileHandle file, const void* src, size_t bytes) override;
    uint64_t FileSize(FileHandle file) const override;
    void FileClose(FileHandle file) override;

    // --- Time.cpp -----------------------------------------------------------
    double GetTimeSeconds() const override;
    void SleepMicros(uint32_t microseconds) override;

    // --- Thread.cpp ---------------------------------------------------------
    PlatformThread* ThreadCreate(ThreadEntry entry, void* userData, size_t stackSize) override;
    void ThreadDestroy(PlatformThread* thread) override;
    PlatformSemaphore* SemaphoreCreate(int32_t initialCount, int32_t maxCount) override;
    void SemaphoreWait(PlatformSemaphore* sema) override;
    void SemaphoreSignal(PlatformSemaphore* sema) override;
    void SemaphoreDestroy(PlatformSemaphore* sema) override;

    // --- Console.cpp --------------------------------------------------------
    void ConsoleWrite(LogLevel level, const char* line) override;
    [[noreturn]] void Panic(const char* message) override;

    // --- Input.cpp ----------------------------------------------------------
    void PollInput() override;
    bool Gamepad_IsConnected(uint8_t port) const override;
    bool Gamepad_IsButtonDown(uint8_t port, GamepadButton button) const override;
    bool Gamepad_WasButtonPressed(uint8_t port, GamepadButton button) const override;
    bool Gamepad_WasButtonReleased(uint8_t port, GamepadButton button) const override;
    Vector2 Gamepad_GetStick(uint8_t port, GamepadStick stick) const override;
    float Gamepad_GetTrigger(uint8_t port, GamepadTrigger trigger) const override;

    bool Keyboard_IsKeyDown(KeyboardKey key) const override;
    bool Keyboard_WasKeyPressed(KeyboardKey key) const override;
    bool Keyboard_WasKeyReleased(KeyboardKey key) const override;

    bool Mouse_IsButtonDown(MouseButton button) const override;
    bool Mouse_WasButtonPressed(MouseButton button) const override;
    Vector2 Mouse_GetPosition() const override;
    Vector2 Mouse_GetDelta() const override;
    float Mouse_GetWheelDelta() const override;
    uint8_t Touch_GetContactCount(TouchSurface surface) const override;
    bool Touch_GetContact(TouchSurface surface, uint8_t index, TouchContact* outContact) const override;
    uint32_t Keyboard_PopCharacters(char* outBuffer, uint32_t bufferSize) override;

    // MessageBoxA blocks the calling thread, so Dialog_Open has already
    // decided the result by the time it returns; the first Dialog_Poll simply
    // reports it. Text input has no host dialog on this platform, only the
    // character channel above, so Dialog_Open refuses DialogKind::TextInput.
    bool Dialog_Open(const DialogRequest& request) override;
    DialogStatus Dialog_Poll() override;
    void Dialog_Cancel() override;

    // --- Window.cpp ---------------------------------------------------------
    bool WindowOpen(const WindowDesc& desc) override;
    void WindowClose() override;
    bool WindowShouldClose() const override;
    void GetFramebufferSize(uint32_t* outWidth, uint32_t* outHeight) const override;
    void* GetNativeWindowHandle() const override;

    // True once since the last call, then cleared: the renderer uses this to
    // know it must reconfigure its swapchain.

    // --- Platform.cpp -------------------------------------------------------
    bool SupportsRenderer(RendererId id) const override;
    RendererId GetDefaultRenderer() const override;
    Renderer* CreateRenderer(RendererId id, const EngineConfig& config) override;
    RendererId GetFallbackRenderer(RendererId failed) const override;
    void DestroyRenderer(Renderer* renderer) override;

private:
    // Locate the directory the executable lives in; assets resolve against it.
    bool ResolveDataRoot();

    // Drain the window message queue. Called from PollInput(), because the
    // queue is where resize, close and wheel events arrive.
    void PumpMessages();

    // Fold the default keyboard layout onto virtual pad 0, so pad-only game
    // code runs unchanged on a desktop. Disabled by --no-keyboard-pad.
    void ApplyKeyboardPadMap();

    struct PadSnapshot
    {
        Vector2 stick[static_cast<uint8_t>(GamepadStick::Count)];
        float trigger[static_cast<uint8_t>(GamepadTrigger::Count)];
        uint16_t buttons; // active-high mask of GamepadButton
        bool connected;
    };

    struct KeyboardSnapshot
    {
        bool keys[static_cast<uint16_t>(KeyboardKey::Count)];
    };

    struct MouseSnapshot
    {
        Vector2 position;
        float wheel;
        bool buttons[static_cast<uint8_t>(MouseButton::Count)];
    };

    // Current and previous frame, so the edge queries (WasPressed / WasReleased)
    // come for free instead of every caller keeping its own "was held" flag.
    PadSnapshot m_pads[MAX_GAME_PAD_PORTS];
    PadSnapshot m_padsPrev[MAX_GAME_PAD_PORTS];
    KeyboardSnapshot m_keys;
    KeyboardSnapshot m_keysPrev;
    MouseSnapshot m_mouse;
    MouseSnapshot m_mousePrev;

    StartupArgs m_startupArgs;
    bool m_keyboardPadMap; // --no-keyboard-pad clears this
    bool m_logInput; // --log-input: report every press, for diagnosing bindings
    char m_dataRoot[512]; // directory the executable lives in; assets resolve against it
    // Aligned allocations come from a different heap than the C allocator here,
    // so Alloc and Free are not interchangeable with malloc and free. That
    // asymmetry is the reason allocation sits behind a contract at all.
    class Win32Memory final : public MemoryContract
    {
    public:
        explicit Win32Memory(const Win32Platform* owner);
        ~Win32Memory() override = default;

        bool Reserve(EngineMemoryMap* outMap) override;
        void Release() override;
        void* Alloc(size_t size, size_t alignment) override;
        void Free(void* ptr) override;
        void GetHeapStats(HeapStats* outStats) const override;
        size_t GetBudgetBytes() const override;

    private:
        const Win32Platform* m_owner;
        void* m_arenaBlock;
        void* m_poolBlock;
        size_t m_reservedBytes;
    };

    Win32Memory m_memory;
    Win32WindowState m_window;
    DialogStatus m_dialogResult;
};
