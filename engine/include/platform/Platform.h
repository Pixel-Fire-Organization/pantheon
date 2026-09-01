#pragma once

#include <cstddef>
#include <cstdint>

#include "PlatformKeys.h"
#include "PlatformTypes.h"
#include "platform/AchievementContract.h"
#include "platform/MemoryContract.h"
#include "graphics/Types.h"

class Renderer;
struct EngineConfig;

class Platform
{
public:
    Platform() = default;
    virtual ~Platform() = default;

    Platform(const Platform&) = delete;
    Platform(Platform&&) = delete;
    Platform& operator=(const Platform&) = delete;
    Platform& operator=(Platform&&) = delete;

    // --- Identity & lifecycle -----------------------------------------------
    virtual PlatformId GetId() const = 0;
    virtual const char* GetName() const = 0;

    // Bring the platform up: console, memory, filesystem, input devices. Called
    // once by Engine_Main before any other method except GetId/GetName.
    virtual bool Init(const StartupArgs& args) = 0;
    virtual void Shutdown() = 0;

    // The arguments this platform was started with, retained for its lifetime.
    virtual const StartupArgs& GetStartupArgs() const = 0;

    // --- Keyed accessors ----------------------------------------------------
    virtual uint32_t GetConstant(PlatformConstant key) const = 0;
    virtual bool HasCapability(PlatformCapability key) const = 0;

    // --- Memory -------------------------------------------------------------
    // Every allocation shared engine code makes goes through this contract. The
    // platform owns the budget, the alignment and the backing allocator.
    // See docs/subsystems/MEMORY.md.
    virtual MemoryContract& GetMemory() = 0;
    virtual const MemoryContract& GetMemory() const = 0;

    /// @return The achievement contract, or null when this platform has none.
    virtual AchievementContract* GetAchievements() = 0;

    // Bytes this platform actually spends on a texture of these dimensions.
    // NOT simply width*height*bpp: the PS2 rounds every mip level up to whole
    // GS pages whose size depends on the pixel format, and PAL8 costs an extra
    // page for its CLUT. A desktop platform returns the real allocation size.
    virtual uint32_t GetTextureFootprintBytes(uint32_t width, uint32_t height, PixelFormat format, uint8_t mipCount) const = 0;

    // --- Filesystem ---------------------------------------------------------
    // Turn a relative asset path into something this platform can open
    // (cdrom0:\PATH;1 on a PS2 disc, an exe-relative path on desktop).
    virtual bool BuildPath(const char* relativePath, char* outBuf, size_t bufSize) const = 0;

    // The active storage token ("cdrom0:", "host:", a data directory...).
    // Assets are addressed relative to it; the resource manager reports it.
    virtual const char* GetResourceToken() const = 0;

    // Turn a relative path into one inside the title's own writable location,
    // creating that location if it does not exist. Answers false where the
    // platform has nowhere to write, which is an ordinary state on a console
    // whose storage is removable — not a fault to recover from.
    virtual bool BuildWritablePath(const char* relativePath, char* outBuf, size_t bufSize) const = 0;

    virtual FileHandle FileOpen(const char* path, FileMode mode) = 0;
    virtual bool FileSeek(FileHandle file, uint64_t offset) = 0;
    virtual size_t FileRead(FileHandle file, void* dst, size_t bytes) = 0;

    // Returns short of the requested count on failure. Platforms reporting no
    // FileWrite capability write nothing and answer zero.
    virtual size_t FileWrite(FileHandle file, const void* src, size_t bytes) = 0;
    virtual uint64_t FileSize(FileHandle file) const = 0;
    virtual void FileClose(FileHandle file) = 0;

    // --- Time ---------------------------------------------------------------
    // Monotonic seconds since an arbitrary epoch, at the best resolution the
    // platform offers. Must not wrap or run backwards within a session.
    virtual double GetTimeSeconds() const = 0;
    virtual void SleepMicros(uint32_t microseconds) = 0;

    // --- Threads & synchronisation ------------------------------------------
    virtual PlatformThread* ThreadCreate(ThreadEntry entry, void* userData, size_t stackSize) = 0;
    virtual void ThreadDestroy(PlatformThread* thread) = 0;

    virtual PlatformSemaphore* SemaphoreCreate(int32_t initialCount, int32_t maxCount) = 0;
    virtual void SemaphoreWait(PlatformSemaphore* sema) = 0;
    virtual void SemaphoreSignal(PlatformSemaphore* sema) = 0;
    virtual void SemaphoreDestroy(PlatformSemaphore* sema) = 0;

    // --- Console & panic ----------------------------------------------------
    // `line` is already formatted and free of control characters.
    virtual void ConsoleWrite(LogLevel level, const char* line) = 0;

    // Unrecoverable. Must not return.
    [[noreturn]] virtual void Panic(const char* message) = 0;

    // --- Input --------------------------------------------------------------
    // Refresh every device snapshot. Called exactly once per frame from
    // Engine_Update; all queries below read that snapshot, so two calls in the
    // same frame always agree.
    virtual void PollInput() = 0;

    virtual bool Gamepad_IsConnected(uint8_t port) const = 0;
    virtual bool Gamepad_IsButtonDown(uint8_t port, GamepadButton button) const = 0;
    virtual bool Gamepad_WasButtonPressed(uint8_t port, GamepadButton button) const = 0;
    virtual bool Gamepad_WasButtonReleased(uint8_t port, GamepadButton button) const = 0;
    virtual Vector2 Gamepad_GetStick(uint8_t port, GamepadStick stick) const = 0;
    virtual float Gamepad_GetTrigger(uint8_t port, GamepadTrigger trigger) const = 0;

    /// @param chord Which debug action to query.
    /// @return Mask of GamepadButton bits that must all be held on port 0, or
    ///         zero when this platform's pad cannot produce that chord.
    virtual uint16_t GetDebugChord(DebugChord chord) const = 0;

    virtual bool Keyboard_IsKeyDown(KeyboardKey key) const = 0;
    virtual bool Keyboard_WasKeyPressed(KeyboardKey key) const = 0;
    virtual bool Keyboard_WasKeyReleased(KeyboardKey key) const = 0;

    virtual bool Mouse_IsButtonDown(MouseButton button) const = 0;
    virtual bool Mouse_WasButtonPressed(MouseButton button) const = 0;
    virtual Vector2 Mouse_GetPosition() const = 0;
    virtual Vector2 Mouse_GetDelta() const = 0;
    virtual float Mouse_GetWheelDelta() const = 0;

    /// @param surface Which panel to query.
    /// @return Live contacts; zero on a platform without touch.
    virtual uint8_t Touch_GetContactCount(TouchSurface surface) const = 0;

    /// @param surface Which panel to query.
    /// @param index Contact index below the current count.
    /// @param outContact Receives the contact, position normalised to [0,1].
    /// @return False when index is past the count, leaving outContact untouched.
    virtual bool Touch_GetContact(TouchSurface surface, uint8_t index, TouchContact* outContact) const = 0;

    // Characters typed since the last call, drained as they are read: a
    // platform with no character channel (PlatformCapability::TextCharacters
    // absent) always returns zero. The stream is printable ASCII plus three
    // control bytes a text-editing widget needs to tell apart from typed
    // text -- 0x08 backspace, 0x0D enter, 0x1B escape -- and nothing else;
    // everything outside that set is dropped here, never handed up, because
    // nothing above this boundary has a substitute glyph for a character
    // stream the way the built-in font has for a single codepoint it cannot
    // draw.
    /// @param outBuffer Receives up to bufferSize bytes, NOT NUL-terminated.
    /// @param bufferSize Capacity of outBuffer.
    /// @return How many bytes were written.
    virtual uint32_t Keyboard_PopCharacters(char* outBuffer, uint32_t bufferSize) = 0;

    // --- System dialogs -------------------------------------------------
    // See PlatformTypes.h for why this is shaped as a non-blocking poll rather
    // than the ordinary call-and-return a Win32 MessageBox would suggest on
    // its own: a Vita dialog cannot be waited on synchronously.

    /// Ask for a system dialog matching `request.kind`. A platform with no
    /// SystemDialog capability, or one that refuses this specific request,
    /// returns false and touches nothing -- the caller's own drawn fallback is
    /// what runs instead, not a second attempt at this call.
    virtual bool Dialog_Open(const DialogRequest& request) = 0;

    /// @return Idle when nothing is open; Pending while still waiting, in
    ///         which case the caller must keep presenting frames; Accepted or
    ///         Cancelled exactly once, the frame the dialog closes.
    virtual DialogStatus Dialog_Poll() = 0;

    /// Abandon whatever Dialog_Open opened. A platform with nothing open
    /// ignores this.
    virtual void Dialog_Cancel() = 0;

    // --- Window / presentation ----------------------------------------------
    // A platform with a fixed framebuffer implements these as stubs that report
    // its constant size and never ask to close.
    virtual bool WindowOpen(const WindowDesc& desc) = 0;
    virtual void WindowClose() = 0;
    virtual bool WindowShouldClose() const = 0;
    virtual void GetFramebufferSize(uint32_t* outWidth, uint32_t* outHeight) const = 0;
    virtual void* GetNativeWindowHandle() const = 0;

    // --- Renderer construction ----------------------------------------------
    // The platform owns which backends exist, because a backend is only ever
    // meaningful on the hardware it targets.
    virtual bool SupportsRenderer(RendererId id) const = 0;
    virtual RendererId GetDefaultRenderer() const = 0;

    // Construct `id`, or return null. Engine_Main walks GetFallbackRenderer()
    // from here when construction fails, so a platform decides its own
    // degradation order (giftag -> ps2gl -> null, webgpu -> opengl -> null).
    virtual Renderer* CreateRenderer(RendererId id, const EngineConfig& config) = 0;
    virtual RendererId GetFallbackRenderer(RendererId failed) const = 0;
    virtual void DestroyRenderer(Renderer* renderer) = 0;
};

// The live platform, installed by Engine_Main before Engine_Init. Mirrors
// Engine_GetRenderer(). Null only before Engine_Main has selected one.
Platform* Engine_GetPlatform();
void Engine_SetPlatform(Platform* platform);
