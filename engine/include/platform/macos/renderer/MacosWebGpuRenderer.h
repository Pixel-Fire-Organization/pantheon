#pragma once

#include "graphics/webgpu/WebGpuRenderer.h"

/// The WebGPU backend presenting through Metal into a macOS window. The surface is a Metal-layer-backed child
/// view of the window's content view, owned by this renderer for exactly as long as the surface lives.
class MacosWebGpuRenderer final : public WebGpuRenderer
{
public:
    MacosWebGpuRenderer() = delete;
    explicit MacosWebGpuRenderer(const EngineConfig& config);
    ~MacosWebGpuRenderer() override = default;

    MacosWebGpuRenderer(const MacosWebGpuRenderer&) = delete;
    MacosWebGpuRenderer(MacosWebGpuRenderer&&) = delete;
    MacosWebGpuRenderer& operator=(const MacosWebGpuRenderer&) = delete;
    MacosWebGpuRenderer& operator=(MacosWebGpuRenderer&&) = delete;

protected:
    /// Adds the Metal-layer child view to the window and wraps its layer.
    /// @return The surface, or null when the platform has no window or the view could not be made.
    WGPUSurface CreateSurface(WGPUInstance instance) override;

    /// Releases the surface, then removes the child view that hosted it.
    void ReleaseSurface(WGPUSurface surface) override;

private:
    /// Retained child view, held as an opaque pointer so this header carries no Objective-C.
    void* m_hostView;
};
