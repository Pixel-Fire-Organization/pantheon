#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstring>

#include "core/EngineDebug.h"
#include "platform/Platform.h"
#include "platform/macos/renderer/MacosWebGpuRenderer.h"

@interface PantheonMetalView : NSView
@end

@implementation PantheonMetalView

- (BOOL)wantsUpdateLayer
{
    return YES;
}

- (BOOL)isOpaque
{
    return YES;
}

- (CALayer*)makeBackingLayer
{
    return [CAMetalLayer layer];
}

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    [[self layer] setContentsScale:1.0];
}

@end

MacosWebGpuRenderer::MacosWebGpuRenderer(const EngineConfig& config) : WebGpuRenderer(config), m_hostView(nullptr) { Initialize(); }

WGPUSurface MacosWebGpuRenderer::CreateSurface(WGPUInstance instance)
{
    NSView* content = (__bridge NSView*)Engine_GetPlatform()->GetNativeWindowHandle();
    if (!content)
    {
        Engine_LogError("MacosWebGpuRenderer: platform has no native window handle");
        return nullptr;
    }

    @autoreleasepool
    {
        PantheonMetalView* view = [[PantheonMetalView alloc] initWithFrame:[content bounds]];
        [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [view setWantsLayer:YES];

        CALayer* layer = [view layer];
        if (![layer isKindOfClass:[CAMetalLayer class]])
        {
            Engine_LogError("MacosWebGpuRenderer: the child view did not produce a Metal layer");
            return nullptr;
        }
        [layer setContentsScale:1.0];
        [content addSubview:view];
        m_hostView = const_cast<void*>(CFBridgingRetain(view));

        WGPUSurfaceSourceMetalLayer fromLayer;
        memset(&fromLayer, 0, sizeof(fromLayer));
        fromLayer.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        fromLayer.layer = (__bridge void*)layer;

        const char* const label = "engine-surface";
        WGPUSurfaceDescriptor surfaceDesc;
        memset(&surfaceDesc, 0, sizeof(surfaceDesc));
        surfaceDesc.nextInChain = &fromLayer.chain;
        surfaceDesc.label = WGPUStringView{label, strlen(label)};

        return wgpuInstanceCreateSurface(instance, &surfaceDesc);
    }
}

void MacosWebGpuRenderer::ReleaseSurface(WGPUSurface surface)
{
    wgpuSurfaceRelease(surface);

    if (m_hostView)
    {
        @autoreleasepool
        {
            NSView* view = (NSView*)CFBridgingRelease(m_hostView);
            [view removeFromSuperview];
        }
        m_hostView = nullptr;
    }
}
