#define GL_SILENCE_DEPRECATION 1

#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>

#include <mach/mach_time.h>
#include <time.h>

#include "core/EngineDebug.h"
#include "platform/Platform.h"
#include "platform/macos/renderer/GlContext.h"

@interface PantheonGLView : NSOpenGLView
@end

@implementation PantheonGLView

- (BOOL)isOpaque
{
    return YES;
}

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    [self setWantsBestResolutionOpenGLSurface:NO];
}

@end

namespace
{

    const double kFallbackRefreshHz = 60.0;

    PantheonGLView* s_View = nil;
    NSOpenGLContext* s_Context = nil;
    uint64_t s_LastSwapNanos = 0;
    mach_timebase_info_data_t s_Timebase;

    uint64_t NowNanos() { return mach_absolute_time() * s_Timebase.numer / s_Timebase.denom; }

    double DisplayRefreshHz()
    {
        NSScreen* screen = [[s_View window] screen];
        if (screen)
        {
            if (@available(macOS 12.0, *))
            {
                const NSInteger hz = [screen maximumFramesPerSecond];
                if (hz > 0)
                    return static_cast<double>(hz);
            }
        }
        return kFallbackRefreshHz;
    }

} // namespace

bool MacosGl_Create()
{
    if (s_View)
        return true;

    NSView* content = (__bridge NSView*)Engine_GetPlatform()->GetNativeWindowHandle();
    if (!content)
    {
        Engine_LogError("OpenGl: platform has no native window handle");
        return false;
    }

    @autoreleasepool
    {
        const NSOpenGLPixelFormatAttribute attributes[] = {NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core, NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, NSOpenGLPFAColorSize, 24, NSOpenGLPFAAlphaSize, 8, NSOpenGLPFADepthSize, 24, 0};
        NSOpenGLPixelFormat* format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
        if (!format)
        {
            Engine_LogError("OpenGl: the driver offers no 4.1 core-profile pixel format");
            return false;
        }

        PantheonGLView* view = [[PantheonGLView alloc] initWithFrame:[content bounds] pixelFormat:format];
        if (!view)
        {
            Engine_LogError("OpenGl: could not create an OpenGL view");
            return false;
        }
        [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [view setWantsBestResolutionOpenGLSurface:NO];
        [content addSubview:view];

        NSOpenGLContext* context = [view openGLContext];
        if (!context)
        {
            [view removeFromSuperview];
            Engine_LogError("OpenGl: the view has no context");
            return false;
        }

        [context makeCurrentContext];
        const GLint interval = 1;
        [context setValues:&interval forParameter:NSOpenGLContextParameterSwapInterval];
        CGLSetParameter([context CGLContextObj], kCGLCPSwapInterval, &interval);
        GLint applied = 0;
        [context getValues:&applied forParameter:NSOpenGLContextParameterSwapInterval];
        if (applied != interval)
            Engine_LogInfo("OpenGl: the driver did not apply a swap interval of %d (reports %d); the frame will not be paced by the display", interval, applied);

        s_View = view;
        s_Context = context;
        mach_timebase_info(&s_Timebase);
        s_LastSwapNanos = 0;
    }
    return true;
}

void MacosGl_Swap()
{
    if (!s_Context)
        return;

    [s_Context flushBuffer];

    const uint64_t frameNanos = static_cast<uint64_t>(1.0e9 / DisplayRefreshHz());
    const uint64_t now = NowNanos();
    const uint64_t target = s_LastSwapNanos + frameNanos;
    if (s_LastSwapNanos != 0 && now < target)
    {
        struct timespec request;
        const uint64_t wait = target - now;
        request.tv_sec = static_cast<time_t>(wait / 1000000000ull);
        request.tv_nsec = static_cast<long>(wait % 1000000000ull);
        nanosleep(&request, nullptr);
        s_LastSwapNanos = target;
    }
    else
    {
        s_LastSwapNanos = now;
    }
}

void MacosGl_Update()
{
    if (s_Context)
        [s_Context update];
}

void MacosGl_Destroy()
{
    if (!s_View)
        return;

    @autoreleasepool
    {
        [NSOpenGLContext clearCurrentContext];
        [s_View clearGLContext];
        [s_View removeFromSuperview];
    }
    s_Context = nil;
    s_View = nil;
}
