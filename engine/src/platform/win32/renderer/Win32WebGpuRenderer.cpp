#include "platform/win32/renderer/Win32WebGpuRenderer.h"

#include <cstring>

#include "core/EngineDebug.h"
#include "platform/Platform.h"

#include <windows.h>

Win32WebGpuRenderer::Win32WebGpuRenderer(const EngineConfig& config) : WebGpuRenderer(config) { Initialize(); }

WGPUSurface Win32WebGpuRenderer::CreateSurface(WGPUInstance instance)
{
    void* hwnd = Engine_GetPlatform()->GetNativeWindowHandle();
    if (!hwnd)
    {
        Engine_LogError("Win32WebGpuRenderer: platform has no native window handle");
        return nullptr;
    }

    WGPUSurfaceSourceWindowsHWND fromHwnd;
    memset(&fromHwnd, 0, sizeof(fromHwnd));
    fromHwnd.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    fromHwnd.hinstance = GetModuleHandleA(nullptr);
    fromHwnd.hwnd = hwnd;

    const char* const label = "engine-surface";
    WGPUSurfaceDescriptor surfaceDesc;
    memset(&surfaceDesc, 0, sizeof(surfaceDesc));
    surfaceDesc.nextInChain = &fromHwnd.chain;
    surfaceDesc.label = WGPUStringView{label, strlen(label)};

    return wgpuInstanceCreateSurface(instance, &surfaceDesc);
}

void Win32WebGpuRenderer::ReleaseSurface(WGPUSurface surface) { wgpuSurfaceRelease(surface); }
