#pragma once

#include "PlatformConstants.h"
#include "core/EngineIO.h"
#include "platform/Platform.h"

// ---------------------------------------------------------------------------
// PspPlatform - the PlayStation Portable target.
//
// One selectable platform, not a family: see docs/psp/PLATFORM.md.
//
// Method bodies are split by concern across Platform.cpp, Memory.cpp, Time.cpp,
// Thread.cpp, Console.cpp, Filesystem.cpp, Input.cpp, Window.cpp and Dialog.cpp.
// ---------------------------------------------------------------------------
class PspPlatform final : public Platform
{
public:
    PspPlatform();
    ~PspPlatform() override = default;

    PspPlatform(const PspPlatform&) = delete;
    PspPlatform(PspPlatform&&) = delete;
    PspPlatform& operator=(const PspPlatform&) = delete;
    PspPlatform& operator=(PspPlatform&&) = delete;

    PlatformId GetId() const override { return PlatformId::Psp; }
    const char* GetName() const override { return "psp"; }

    bool Init(const StartupArgs& args) override;
    void Shutdown() override;
    const StartupArgs& GetStartupArgs() const override;

    uint32_t GetConstant(PlatformConstant key) const override;
    bool HasCapability(PlatformCapability key) const override;

    /// @param chord Which debug action to query.
    /// @return A mask using only the buttons this pad physically has: one
    ///         shoulder row, no stick clicks.
    uint16_t GetDebugChord(DebugChord chord) const override;

    // --- Memory.cpp ---------------------------------------------------------
    /// @return Null; this hardware has no achievement service.
    AchievementContract* GetAchievements() override { return nullptr; }

    MemoryContract& GetMemory() override { return m_memory; }
    const MemoryContract& GetMemory() const override { return m_memory; }
    uint32_t GetTextureFootprintBytes(uint32_t width, uint32_t height, PixelFormat format, uint8_t mipCount) const override;

    // --- Filesystem.cpp -----------------------------------------------------
    bool BuildPath(const char* relativePath, char* outBuf, size_t bufSize) const override;
    bool BuildWritablePath(const char* relativePath, char* outBuf, size_t bufSize) const override;
    const char* GetResourceToken() const override { return m_resourceToken; }
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

    /// Close the log file, if one was opened. Called from Shutdown.
    void CloseLog();

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

    // --- Dialog.cpp ---------------------------------------------------------
    bool Dialog_Open(const DialogRequest& request) override;
    DialogStatus Dialog_Poll() override;
    void Dialog_Cancel() override;

    // --- Window.cpp ---------------------------------------------------------
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

private:
    /// Decide which of the three storage roots this process was launched from
    /// and fill m_resourceToken and m_writableRoot accordingly.
    /// @param launchPath argv[0], or null when the host supplied none.
    void ResolveDeviceToken(const char* launchPath);

    /// Translate one engine asset key into this platform's path convention,
    /// writing into outBuf.
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

    StartupArgs m_startupArgs;
    bool m_logInput;
    bool m_padReported;
    bool m_initialised;

    const char* m_resourceToken;
    char m_resourceRoot[IO_FILE_MAX_PATH];
    char m_writableRoot[IO_FILE_MAX_PATH];

    DialogKind m_dialogKind;
    char* m_dialogResultBuffer;
    size_t m_dialogResultBufferSize;

    /// The engine map is reserved from the system partition rather than from
    /// the C heap, which is itself carved out of the same partition. Alloc and
    /// Free are not interchangeable with malloc and free.
    class PspMemory final : public MemoryContract
    {
    public:
        explicit PspMemory(const PspPlatform* owner);
        ~PspMemory() override = default;

        bool Reserve(EngineMemoryMap* outMap) override;
        void Release() override;
        void* Alloc(size_t size, size_t alignment) override;
        void Free(void* ptr) override;
        void GetHeapStats(HeapStats* outStats) const override;
        size_t GetBudgetBytes() const override;

    private:
        const PspPlatform* m_owner;
        int32_t m_arenaBlockId;
        int32_t m_poolBlockId;
        void* m_arenaBlock;
        void* m_poolBlock;
        size_t m_reservedBytes;
    };

    PspMemory m_memory;
};
