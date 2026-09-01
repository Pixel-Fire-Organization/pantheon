#include "Macros.h"
#include "Platform.h"

bool VitaPlatform::WindowOpen(const WindowDesc& desc)
{
    UNUSED_VAR(desc);
    return true;
}

void VitaPlatform::WindowClose() {}

bool VitaPlatform::WindowShouldClose() const { return false; }

void VitaPlatform::GetFramebufferSize(uint32_t* outWidth, uint32_t* outHeight) const
{
    if (outWidth)
        *outWidth = GFX_SCREEN_WIDTH;
    if (outHeight)
        *outHeight = GFX_SCREEN_HEIGHT;
}

void* VitaPlatform::GetNativeWindowHandle() const { return nullptr; }
