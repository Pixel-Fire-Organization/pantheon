#pragma once

#include "graphics/webgpu/WebGpuRenderer.h"

/// The WebGPU backend presenting into a Win32 window.
class Win32WebGpuRenderer final : public WebGpuRenderer
{
public:
    Win32WebGpuRenderer() = delete;
    explicit Win32WebGpuRenderer(const EngineConfig& config);
    ~Win32WebGpuRenderer() override = default;

    Win32WebGpuRenderer(const Win32WebGpuRenderer&) = delete;
    Win32WebGpuRenderer(Win32WebGpuRenderer&&) = delete;
    Win32WebGpuRenderer& operator=(const Win32WebGpuRenderer&) = delete;
    Win32WebGpuRenderer& operator=(Win32WebGpuRenderer&&) = delete;

protected:
    /// @return A surface over the platform's window handle, or null when the platform has none.
    WGPUSurface CreateSurface(WGPUInstance instance) override;

    /// Releases the surface; the window itself belongs to the platform.
    void ReleaseSurface(WGPUSurface surface) override;
};
