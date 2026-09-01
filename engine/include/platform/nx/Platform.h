#pragma once

#include "PlatformConstants.h"
#include "core/EngineIO.h"
#include "platform/Platform.h"

/// The Nintendo Switch target: one platform whose framebuffer follows the
/// console's operation mode. See docs/nx/PLATFORM.md.
class NxPlatform final : public Platform
{
public:
    NxPlatform();
    ~NxPlatform() override = default;

    NxPlatform(const NxPlatform&) = delete;
    NxPlatform(NxPlatform&&) = delete;
    NxPlatform& operator=(const NxPlatform&) = delete;
    NxPlatform& operator=(NxPlatform&&) = delete;

    PlatformId GetId() const override { return PlatformId::Nx; }
    const char* GetName() const override { return "nx"; }

    bool Init(const StartupArgs& args) override;
    void Shutdown() override;
    const StartupArgs& GetStartupArgs() const override;

    uint32_t GetConstant(PlatformConstant key) const override;
    bool HasCapability(PlatformCapability key) const override;

    /// @param chord Which debug action to query.
    /// @return A mask of full-pad buttons; every controller style this platform accepts has all of them.
    uint16_t GetDebugChord(DebugChord chord) const override;

    /// @return Null; homebrew has no access to the system achievement service.
    AchievementContract* GetAchievements() override { return nullptr; }

    MemoryContract& GetMemory() override { return m_memory; }
    const MemoryContract& GetMemory() const override { return m_memory; }

    /// @return RGBA8 bytes across every mip level, rounded up to the graphics driver's page size.
    uint32_t GetTextureFootprintBytes(uint32_t width, uint32_t height, PixelFormat format, uint8_t mipCount) const override;

    bool BuildPath(const char* relativePath, char* outBuf, size_t bufSize) const override;
    bool BuildWritablePath(const char* relativePath, char* outBuf, size_t bufSize) const override;
    const char* GetResourceToken() const override { return m_resourceToken; }
    FileHandle FileOpen(const char* path, FileMode mode) override;
    bool FileSeek(FileHandle file, uint64_t offset) override;
    size_t FileRead(FileHandle file, void* dst, size_t bytes) override;
    size_t FileWrite(FileHandle file, const void* src, size_t bytes) override;
    uint64_t FileSize(FileHandle file) const override;
    void FileClose(FileHandle file) override;

    /// @return Seconds since start-up, excluding time the console spent with the title suspended.
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

    /// Close the log file, if one was opened.
    void CloseLog();

    /// Service the system's applet messages, then read every controller and the touchscreen.
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

    /// Text input runs the software keyboard applet, which blocks until the player finishes;
    /// message and confirmation boxes are refused so the interface draws them.
    bool Dialog_Open(const DialogRequest& request) override;
    DialogStatus Dialog_Poll() override;
    void Dialog_Cancel() override;

    bool WindowOpen(const WindowDesc& desc) override;
    void WindowClose() override;
    bool WindowShouldClose() const override;

    /// @param outWidth Receives 1280 handheld or 1920 docked.
    /// @param outHeight Receives 720 handheld or 1080 docked.
    void GetFramebufferSize(uint32_t* outWidth, uint32_t* outHeight) const override;

    /// @return The system's default native window, which both renderers attach their display buffers to.
    void* GetNativeWindowHandle() const override;

    bool SupportsRenderer(RendererId id) const override;
    RendererId GetDefaultRenderer() const override;
    Renderer* CreateRenderer(RendererId id, const EngineConfig& config) override;
    RendererId GetFallbackRenderer(RendererId failed) const override;
    void DestroyRenderer(Renderer* renderer) override;

private:
    /// Take the process out of the system's default focus handling so focus loss is seen before suspension.
    void InitApplet();

    /// Restore the system's default focus handling and allow the system to end the process.
    void ShutdownApplet();

    /// Process every pending applet message: exit requests, focus changes and operation-mode changes.
    void PumpAppletMessages();

    /// Declare the accepted controller styles and open the controllers and the touchscreen.
    void InitInput();

    /// Read the operation mode and update the cached framebuffer size and docked flag.
    void RefreshOperationMode();

    /// Create the writable directory if it does not exist.
    /// @return False when the SD card is not mounted or the directory cannot be created.
    bool EnsureWritableRoot() const;

    /// Join a root and an engine asset key, translating separators.
    /// @return False when the result would not fit.
    bool AppendResolved(const char* root, const char* relativePath, char* outBuf, size_t bufSize) const;

    struct PadSnapshot
    {
        Vector2 stick[static_cast<uint8_t>(GamepadStick::Count)];
        float trigger[static_cast<uint8_t>(GamepadTrigger::Count)];
        uint16_t buttons;
        bool connected;
    };

    PadSnapshot m_pads[MAX_GAME_PAD_PORTS];
    PadSnapshot m_padsPrev[MAX_GAME_PAD_PORTS];

    TouchContact m_touches[INPUT_TOUCH_MAX_CONTACTS];
    uint8_t m_touchCount;

    StartupArgs m_startupArgs;

    const char* m_resourceToken;
    char m_writableRoot[IO_FILE_MAX_PATH];

    uint64_t m_tickOrigin;
    uint64_t m_suspendedTicks;
    uint64_t m_suspendStartTick;

    uint32_t m_framebufferWidth;
    uint32_t m_framebufferHeight;

    DialogStatus m_dialogResult;

    bool m_suspended;
    bool m_docked;
    bool m_exitRequested;
    bool m_romfsMounted;
    bool m_sdMounted;
    bool m_exitLocked;
    bool m_logInput;
    bool m_initialised;

    /// Every allocation, the engine map included, comes from the one C heap libnx gives the whole
    /// process allowance to; Alloc and Free are that heap's aligned pair.
    class NxMemory final : public MemoryContract
    {
    public:
        explicit NxMemory(const NxPlatform* owner);
        ~NxMemory() override = default;

        bool Reserve(EngineMemoryMap* outMap) override;
        void Release() override;
        void* Alloc(size_t size, size_t alignment) override;
        void Free(void* ptr) override;
        void GetHeapStats(HeapStats* outStats) const override;
        size_t GetBudgetBytes() const override;

    private:
        const NxPlatform* m_owner;
        void* m_arenaBlock;
        void* m_poolBlock;
        size_t m_reservedBytes;
    };

    NxMemory m_memory;
};
