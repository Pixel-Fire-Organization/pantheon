#include "Macros.h"
#include "Platform.h"

// ---------------------------------------------------------------------------
// The PS2 has no window: the GS framebuffer is a fixed size chosen at build time
// by the region, and the renderer owns video-mode setup. These are honest stubs
// that report that size and never ask to close - the desktop platforms are where
// this interface earns its keep.
// ---------------------------------------------------------------------------

bool Ps2Platform::WindowOpen(const WindowDesc& desc)
{
    UNUSED_VAR(desc);
    return true;
}

void Ps2Platform::WindowClose() {}

bool Ps2Platform::WindowShouldClose() const
{
    // A console has no close button; the loop ends only via game::Exit().
    return false;
}

void Ps2Platform::GetFramebufferSize(uint32_t* outWidth, uint32_t* outHeight) const
{
    if (outWidth)
        *outWidth = GFX_SCREEN_WIDTH;
    if (outHeight)
        *outHeight = GFX_SCREEN_HEIGHT;
}

void* Ps2Platform::GetNativeWindowHandle() const { return nullptr; }
