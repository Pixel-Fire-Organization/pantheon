#pragma once

#include "PlatformConstants.h"
#include "platform/Platform.h"

/// Window and raw input state written by the event pump and read by PollInput.
/// Keys are indexed by KeyboardKey. A hit latches a press that arrived since the last poll, so a key
/// tapped and released inside one frame is still seen.
struct MacosWindowState
{
    float wheelDelta;
    float mouseX;
    float mouseY;
    uint32_t width;
    uint32_t height;
    uint32_t charCount;
    bool shouldClose;
    bool logInput;
    bool keyDown[static_cast<uint16_t>(KeyboardKey::Count)];
    bool keyHit[static_cast<uint16_t>(KeyboardKey::Count)];
    bool mouseDown[8];
    bool mouseHit[8];

    enum : uint32_t
    {
        CHAR_BUFFER_SIZE = 64
    };
    char charBuffer[CHAR_BUFFER_SIZE];
};

/// The native macOS target: a Cocoa window, keyboard, mouse and game controllers, on one binary that
/// carries both Intel and Apple Silicon code. Method bodies are split by concern across Platform.cpp,
/// Memory.cpp, Time.cpp, Thread.cpp, Console.cpp, Filesystem.cpp, Input.mm, Window.mm and Dialog.mm.
class MacosPlatform final : public Platform
{
public:
    MacosPlatform();
    ~MacosPlatform() override = default;

    MacosPlatform(const MacosPlatform&) = delete;
    MacosPlatform(MacosPlatform&&) = delete;
    MacosPlatform& operator=(const MacosPlatform&) = delete;
    MacosPlatform& operator=(MacosPlatform&&) = delete;

    PlatformId GetId() const override { return PlatformId::Macos; }
    const char* GetName() const override { return "macos"; }

    bool Init(const StartupArgs& args) override;
    void Shutdown() override;
    const StartupArgs& GetStartupArgs() const override;

    uint32_t GetConstant(PlatformConstant key) const override;
    bool HasCapability(PlatformCapability key) const override;

    /// @param chord Which debug action to query.
    /// @return The full-pad mask; the keyboard bridge covers every button.
    uint16_t GetDebugChord(DebugChord chord) const override;

    /// @return Null; this platform has no achievements.
    AchievementContract* GetAchievements() override { return nullptr; }

    MemoryContract& GetMemory() override { return m_memory; }
    const MemoryContract& GetMemory() const override { return m_memory; }
    uint32_t GetTextureFootprintBytes(uint32_t width, uint32_t height, PixelFormat format, uint8_t mipCount) const override;

    bool BuildPath(const char* relativePath, char* outBuf, size_t bufSize) const override;
    bool BuildWritablePath(const char* relativePath, char* outBuf, size_t bufSize) const override;
    const char* GetResourceToken() const override { return m_dataRoot; }
    FileHandle FileOpen(const char* path, FileMode mode) override;
    bool FileSeek(FileHandle file, uint64_t offset) override;
    size_t FileRead(FileHandle file, void* dst, size_t bytes) override;
    size_t FileWrite(FileHandle file, const void* src, size_t bytes) override;
    uint64_t FileSize(FileHandle file) const override;
    void FileClose(FileHandle file) override;

    double GetTimeSeconds() const override;
    void SleepMicros(uint32_t microseconds) override;

    PlatformThread* ThreadCreate(ThreadEntry entry, void* userData, size_t stackSize) override;
    void ThreadDestroy(PlatformThread* thread) override;
    PlatformSemaphore* SemaphoreCreate(int32_t initialCount, int32_t maxCount) override;
    void SemaphoreWait(PlatformSemaphore* sema) override;
    void SemaphoreSignal(PlatformSemaphore* sema) override;
    void SemaphoreDestroy(PlatformSemaphore* sema) override;

    void ConsoleWrite(LogLevel level, const char* line) override;
    [[noreturn]] void Panic(const char* message) override;

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

    /// Message and Confirm run a modal alert on the calling thread and report the outcome on the first
    /// poll. TextInput is refused: the character channel serves text entry inline.
    bool Dialog_Open(const DialogRequest& request) override;
    DialogStatus Dialog_Poll() override;
    void Dialog_Cancel() override;

    bool WindowOpen(const WindowDesc& desc) override;
    void WindowClose() override;
    bool WindowShouldClose() const override;
    void GetFramebufferSize(uint32_t* outWidth, uint32_t* outHeight) const override;

    /// @return The window's content view as an opaque pointer; renderers add their own child view to it.
    void* GetNativeWindowHandle() const override;

    bool SupportsRenderer(RendererId id) const override;
    RendererId GetDefaultRenderer() const override;
    Renderer* CreateRenderer(RendererId id, const EngineConfig& config) override;
    RendererId GetFallbackRenderer(RendererId failed) const override;
    void DestroyRenderer(Renderer* renderer) override;

private:
    /// Resolve the directory assets load from: Contents/Resources inside an application bundle,
    /// the executable's own directory otherwise.
    bool ResolveDataRoot();

    /// Drain the event queue into the window state. Called from PollInput.
    void PumpMessages();

    /// Fold the default keyboard layout onto virtual pad 0. Disabled by --no-keyboard-pad.
    void ApplyKeyboardPadMap();

    /// Read every connected game controller into the pad snapshots.
    void PollControllers();

    struct PadSnapshot
    {
        Vector2 stick[static_cast<uint8_t>(GamepadStick::Count)];
        float trigger[static_cast<uint8_t>(GamepadTrigger::Count)];
        uint16_t buttons;
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

    PadSnapshot m_pads[MAX_GAME_PAD_PORTS];
    PadSnapshot m_padsPrev[MAX_GAME_PAD_PORTS];
    KeyboardSnapshot m_keys;
    KeyboardSnapshot m_keysPrev;
    MouseSnapshot m_mouse;
    MouseSnapshot m_mousePrev;

    StartupArgs m_startupArgs;
    bool m_keyboardPadMap;
    bool m_logInput;
    char m_dataRoot[512];

    class MacosMemory final : public MemoryContract
    {
    public:
        explicit MacosMemory(const MacosPlatform* owner);
        ~MacosMemory() override = default;

        bool Reserve(EngineMemoryMap* outMap) override;
        void Release() override;
        void* Alloc(size_t size, size_t alignment) override;
        void Free(void* ptr) override;
        void GetHeapStats(HeapStats* outStats) const override;
        size_t GetBudgetBytes() const override;

    private:
        const MacosPlatform* m_owner;
        void* m_arenaBlock;
        void* m_poolBlock;
        size_t m_reservedBytes;
    };

    MacosMemory m_memory;
    MacosWindowState m_window;
    DialogStatus m_dialogResult;
};
