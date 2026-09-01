#include "Macros.h"
#include "Platform.h"

#include <switch.h>

bool NxPlatform::WindowOpen(const WindowDesc& desc)
{
    UNUSED_VAR(desc);
    return true;
}

void NxPlatform::WindowClose() {}

bool NxPlatform::WindowShouldClose() const { return m_exitRequested; }

void NxPlatform::GetFramebufferSize(uint32_t* outWidth, uint32_t* outHeight) const
{
    if (outWidth)
        *outWidth = m_framebufferWidth;
    if (outHeight)
        *outHeight = m_framebufferHeight;
}

void* NxPlatform::GetNativeWindowHandle() const { return nwindowGetDefault(); }
