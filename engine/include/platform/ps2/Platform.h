#pragma once

#include "PlatformConstants.h"
#include "platform/Platform.h"

class Ps2Platform : public Platform
{
public:
    Ps2Platform();
    ~Ps2Platform() override = default;

    Ps2Platform(const Ps2Platform&) = delete;
    Ps2Platform(Ps2Platform&&) = delete;
    Ps2Platform& operator=(const Ps2Platform&) = delete;
    Ps2Platform& operator=(Ps2Platform&&) = delete;

    // GetId() / GetName() stay pure - that is what keeps this class abstract.

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
    FileHandle FileOpen(const char* path, FileMode mode) override;
    bool FileSeek(FileHandle file, uint64_t offset) override;
    size_t FileRead(FileHandle file, void* dst, size_t bytes) override;
    size_t FileWrite(FileHandle file, const void* src, size_t bytes) override;
    uint64_t FileSize(FileHandle file) const override;
    void FileClose(FileHandle file) override;

    // Derived from argv[0] in Init: "cdrom0:", "mass0:", "hdd0:" or "host:".
    const char* GetResourceToken() const override { return m_deviceToken; }

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

    // The PS2 has no keyboard or mouse in this engine's supported configuration.
    // These are honest stubs, and HasCapability reports them as absent - the
    // platform never pretends a pad is a keyboard.
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

    // No character channel and no system dialog service: the interface's own
    // drawn fallback is the whole of text entry here.
    bool Dialog_Open(const DialogRequest& request) override;
    DialogStatus Dialog_Poll() override;
    void Dialog_Cancel() override;

    // --- Window.cpp ---------------------------------------------------------
    // The GS framebuffer is fixed and owned by the renderer, so these report the
    // compile-time size and never ask to close.
    bool WindowOpen(const WindowDesc& desc) override;
    void WindowClose() override;
    bool WindowShouldClose() const override;
    void GetFramebufferSize(uint32_t* outWidth, uint32_t* outHeight) const override;
    void* GetNativeWindowHandle() const override;

    // --- Platform.cpp -------------------------------------------------------
    bool SupportsRenderer(RendererId id) const override;
    RendererId GetDefaultRenderer() const override;
    Renderer* CreateRenderer(RendererId id, const EngineConfig& config) override;
    RendererId GetFallbackRenderer(RendererId failed) const override;
    void DestroyRenderer(Renderer* renderer) override;

protected:
    // Map an argv[0]-style boot path onto a storage token. Matches only the
    // leading device name: "cdrom0:\MAIN.ELF;1" -> "cdrom0:".
    static const char* ResolveDeviceToken(const char* bootPath);

    // Close any pads opened by PollInput(). Called from Shutdown().
    void ShutdownInput();

    // One snapshot of every pad, refreshed by PollInput() once per frame.
    // Keeping the previous frame alongside the current one is what makes the
    // WasPressed / WasReleased edge queries possible without callers hand-rolling
    // their own "was held" flags.
    struct PadSnapshot
    {
        Vector2 stick[static_cast<uint8_t>(GamepadStick::Count)];
        uint16_t buttons; // active-high mask of GamepadButton
        bool connected;
    };

    PadSnapshot m_pads[MAX_GAME_PAD_PORTS];
    PadSnapshot m_padsPrev[MAX_GAME_PAD_PORTS];

    // EE RAM is a hard ceiling with no virtual memory behind it, so the budget
    // check in Reserve is the only thing standing between an over-sized map and
    // corruption in an unrelated subsystem.
    class Ps2Memory final : public MemoryContract
    {
    public:
        explicit Ps2Memory(const Ps2Platform* owner);
        ~Ps2Memory() override = default;

        bool Reserve(EngineMemoryMap* outMap) override;
        void Release() override;
        void* Alloc(size_t size, size_t alignment) override;
        void Free(void* ptr) override;
        void GetHeapStats(HeapStats* outStats) const override;
        size_t GetBudgetBytes() const override;

    private:
        const Ps2Platform* m_owner;
        void* m_arenaBlock;
        void* m_poolBlock;
    };

    StartupArgs m_startupArgs;
    const char* m_deviceToken;
    Ps2Memory m_memory;
    bool m_initialised;
};
