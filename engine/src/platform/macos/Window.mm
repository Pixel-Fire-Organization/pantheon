#import <Cocoa/Cocoa.h>

#include <cstring>

#include "Platform.h"
#include "core/EngineDebug.h"

namespace
{

    enum : unsigned short
    {
        VK_A = 0x00,
        VK_S = 0x01,
        VK_D = 0x02,
        VK_F = 0x03,
        VK_H = 0x04,
        VK_G = 0x05,
        VK_Z = 0x06,
        VK_X = 0x07,
        VK_C = 0x08,
        VK_V = 0x09,
        VK_B = 0x0B,
        VK_Q = 0x0C,
        VK_W = 0x0D,
        VK_E = 0x0E,
        VK_R = 0x0F,
        VK_Y = 0x10,
        VK_T = 0x11,
        VK_1 = 0x12,
        VK_2 = 0x13,
        VK_3 = 0x14,
        VK_4 = 0x15,
        VK_6 = 0x16,
        VK_5 = 0x17,
        VK_EQUAL = 0x18,
        VK_9 = 0x19,
        VK_7 = 0x1A,
        VK_MINUS = 0x1B,
        VK_8 = 0x1C,
        VK_0 = 0x1D,
        VK_RIGHT_BRACKET = 0x1E,
        VK_O = 0x1F,
        VK_U = 0x20,
        VK_LEFT_BRACKET = 0x21,
        VK_I = 0x22,
        VK_P = 0x23,
        VK_RETURN = 0x24,
        VK_L = 0x25,
        VK_J = 0x26,
        VK_QUOTE = 0x27,
        VK_K = 0x28,
        VK_SEMICOLON = 0x29,
        VK_BACKSLASH = 0x2A,
        VK_COMMA = 0x2B,
        VK_SLASH = 0x2C,
        VK_N = 0x2D,
        VK_M = 0x2E,
        VK_PERIOD = 0x2F,
        VK_TAB = 0x30,
        VK_SPACE = 0x31,
        VK_GRAVE = 0x32,
        VK_DELETE = 0x33,
        VK_ESCAPE = 0x35,
        VK_SHIFT = 0x38,
        VK_OPTION = 0x3A,
        VK_CONTROL = 0x3B,
        VK_RIGHT_SHIFT = 0x3C,
        VK_RIGHT_OPTION = 0x3D,
        VK_RIGHT_CONTROL = 0x3E,
        VK_KEYPAD_ENTER = 0x4C,
        VK_F5 = 0x60,
        VK_F6 = 0x61,
        VK_F7 = 0x62,
        VK_F3 = 0x63,
        VK_F8 = 0x64,
        VK_F9 = 0x65,
        VK_F11 = 0x67,
        VK_F10 = 0x6D,
        VK_F12 = 0x6F,
        VK_HELP = 0x72,
        VK_HOME = 0x73,
        VK_PAGE_UP = 0x74,
        VK_FORWARD_DELETE = 0x75,
        VK_F4 = 0x76,
        VK_END = 0x77,
        VK_F2 = 0x78,
        VK_PAGE_DOWN = 0x79,
        VK_F1 = 0x7A,
        VK_LEFT = 0x7B,
        VK_RIGHT = 0x7C,
        VK_DOWN = 0x7D,
        VK_UP = 0x7E
    };

    const NSUInteger kLeftShiftMask = 0x00000002;
    const NSUInteger kRightShiftMask = 0x00000004;
    const NSUInteger kLeftControlMask = 0x00000001;
    const NSUInteger kRightControlMask = 0x00002000;
    const NSUInteger kLeftOptionMask = 0x00000020;
    const NSUInteger kRightOptionMask = 0x00000040;

    const float kPreciseScrollPerNotch = 10.0f;
    const unsigned kMinimumWidth = 320;
    const unsigned kMinimumHeight = 240;

    MacosWindowState* s_State = nullptr;
    NSWindow* s_Window = nil;
    NSView* s_ContentView = nil;
    id s_Delegate = nil;
    NSString* s_AppName = nil;

    KeyboardKey KeyForCode(unsigned short code)
    {
        switch (code)
        {
        case VK_A:
            return KeyboardKey::A;
        case VK_B:
            return KeyboardKey::B;
        case VK_C:
            return KeyboardKey::C;
        case VK_D:
            return KeyboardKey::D;
        case VK_E:
            return KeyboardKey::E;
        case VK_F:
            return KeyboardKey::F;
        case VK_G:
            return KeyboardKey::G;
        case VK_H:
            return KeyboardKey::H;
        case VK_I:
            return KeyboardKey::I;
        case VK_J:
            return KeyboardKey::J;
        case VK_K:
            return KeyboardKey::K;
        case VK_L:
            return KeyboardKey::L;
        case VK_M:
            return KeyboardKey::M;
        case VK_N:
            return KeyboardKey::N;
        case VK_O:
            return KeyboardKey::O;
        case VK_P:
            return KeyboardKey::P;
        case VK_Q:
            return KeyboardKey::Q;
        case VK_R:
            return KeyboardKey::R;
        case VK_S:
            return KeyboardKey::S;
        case VK_T:
            return KeyboardKey::T;
        case VK_U:
            return KeyboardKey::U;
        case VK_V:
            return KeyboardKey::V;
        case VK_W:
            return KeyboardKey::W;
        case VK_X:
            return KeyboardKey::X;
        case VK_Y:
            return KeyboardKey::Y;
        case VK_Z:
            return KeyboardKey::Z;

        case VK_0:
            return KeyboardKey::Num0;
        case VK_1:
            return KeyboardKey::Num1;
        case VK_2:
            return KeyboardKey::Num2;
        case VK_3:
            return KeyboardKey::Num3;
        case VK_4:
            return KeyboardKey::Num4;
        case VK_5:
            return KeyboardKey::Num5;
        case VK_6:
            return KeyboardKey::Num6;
        case VK_7:
            return KeyboardKey::Num7;
        case VK_8:
            return KeyboardKey::Num8;
        case VK_9:
            return KeyboardKey::Num9;

        case VK_F1:
            return KeyboardKey::F1;
        case VK_F2:
            return KeyboardKey::F2;
        case VK_F3:
            return KeyboardKey::F3;
        case VK_F4:
            return KeyboardKey::F4;
        case VK_F5:
            return KeyboardKey::F5;
        case VK_F6:
            return KeyboardKey::F6;
        case VK_F7:
            return KeyboardKey::F7;
        case VK_F8:
            return KeyboardKey::F8;
        case VK_F9:
            return KeyboardKey::F9;
        case VK_F10:
            return KeyboardKey::F10;
        case VK_F11:
            return KeyboardKey::F11;
        case VK_F12:
            return KeyboardKey::F12;

        case VK_LEFT:
            return KeyboardKey::Left;
        case VK_RIGHT:
            return KeyboardKey::Right;
        case VK_UP:
            return KeyboardKey::Up;
        case VK_DOWN:
            return KeyboardKey::Down;

        case VK_SPACE:
            return KeyboardKey::Space;
        case VK_RETURN:
        case VK_KEYPAD_ENTER:
            return KeyboardKey::Enter;
        case VK_ESCAPE:
            return KeyboardKey::Escape;
        case VK_TAB:
            return KeyboardKey::Tab;
        case VK_DELETE:
            return KeyboardKey::Backspace;
        case VK_FORWARD_DELETE:
            return KeyboardKey::Delete;
        case VK_HELP:
            return KeyboardKey::Insert;
        case VK_HOME:
            return KeyboardKey::Home;
        case VK_END:
            return KeyboardKey::End;
        case VK_PAGE_UP:
            return KeyboardKey::PageUp;
        case VK_PAGE_DOWN:
            return KeyboardKey::PageDown;

        case VK_MINUS:
            return KeyboardKey::Minus;
        case VK_EQUAL:
            return KeyboardKey::Equal;
        case VK_LEFT_BRACKET:
            return KeyboardKey::LeftBracket;
        case VK_RIGHT_BRACKET:
            return KeyboardKey::RightBracket;
        case VK_SEMICOLON:
            return KeyboardKey::Semicolon;
        case VK_QUOTE:
            return KeyboardKey::Apostrophe;
        case VK_COMMA:
            return KeyboardKey::Comma;
        case VK_PERIOD:
            return KeyboardKey::Period;
        case VK_SLASH:
            return KeyboardKey::Slash;
        case VK_BACKSLASH:
            return KeyboardKey::Backslash;
        case VK_GRAVE:
            return KeyboardKey::Grave;

        default:
            return KeyboardKey::Unknown;
        }
    }

    void SetKey(KeyboardKey key, bool down)
    {
        if (key == KeyboardKey::Unknown)
            return;
        const uint16_t index = static_cast<uint16_t>(key);
        s_State->keyDown[index] = down;
        if (down)
            s_State->keyHit[index] = true;
    }

    void HandleModifier(NSEvent* event)
    {
        const NSUInteger flags = [event modifierFlags];
        switch ([event keyCode])
        {
        case VK_SHIFT:
            SetKey(KeyboardKey::LeftShift, (flags & kLeftShiftMask) != 0);
            break;
        case VK_RIGHT_SHIFT:
            SetKey(KeyboardKey::RightShift, (flags & kRightShiftMask) != 0);
            break;
        case VK_CONTROL:
            SetKey(KeyboardKey::LeftControl, (flags & kLeftControlMask) != 0);
            break;
        case VK_RIGHT_CONTROL:
            SetKey(KeyboardKey::RightControl, (flags & kRightControlMask) != 0);
            break;
        case VK_OPTION:
            SetKey(KeyboardKey::LeftAlt, (flags & kLeftOptionMask) != 0);
            break;
        case VK_RIGHT_OPTION:
            SetKey(KeyboardKey::RightAlt, (flags & kRightOptionMask) != 0);
            break;
        default:
            break;
        }
    }

    void PushCharacter(char c)
    {
        if (s_State->charCount < MacosWindowState::CHAR_BUFFER_SIZE)
            s_State->charBuffer[s_State->charCount++] = c;
    }

    void HandleCharacters(NSEvent* event)
    {
        const NSEventModifierFlags flags = [event modifierFlags];
        if (flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl))
            return;

        NSString* characters = [event characters];
        for (NSUInteger i = 0; i < [characters length]; ++i)
        {
            const unichar unit = [characters characterAtIndex:i];
            if (unit >= 32 && unit < 127)
                PushCharacter(static_cast<char>(unit));
            else if (unit == 0x7F)
                PushCharacter(8);
            else if (unit == 13 || unit == 3)
                PushCharacter(13);
            else if (unit == 27)
                PushCharacter(27);
        }
    }

    void SetMouseButton(NSInteger button, bool down)
    {
        if (button < 0 || button >= 8)
            return;
        s_State->mouseDown[button] = down;
        if (down)
            s_State->mouseHit[button] = true;
    }

    bool InContent(NSEvent* event)
    {
        if ([event window] != s_Window || !s_ContentView)
            return false;
        return NSPointInRect([event locationInWindow], [s_ContentView frame]);
    }

    void ClearHeldInput()
    {
        memset(s_State->keyDown, 0, sizeof(s_State->keyDown));
        memset(s_State->mouseDown, 0, sizeof(s_State->mouseDown));
    }

    void UpdateSizeFromView()
    {
        if (!s_ContentView)
            return;
        const NSSize size = [s_ContentView bounds].size;
        if (size.width >= 1.0 && size.height >= 1.0)
        {
            s_State->width = static_cast<uint32_t>(size.width);
            s_State->height = static_cast<uint32_t>(size.height);
        }
    }

    void HandleEvent(NSEvent* event)
    {
        if (s_State->logInput && ([event type] == NSEventTypeKeyDown || [event type] == NSEventTypeFlagsChanged))
            Engine_LogInfo("input: key event code=0x%02X", static_cast<unsigned>([event keyCode]));

        switch ([event type])
        {
        case NSEventTypeKeyDown:
            if (![event isARepeat])
                SetKey(KeyForCode([event keyCode]), true);
            HandleCharacters(event);
            break;
        case NSEventTypeKeyUp:
            SetKey(KeyForCode([event keyCode]), false);
            break;
        case NSEventTypeFlagsChanged:
            HandleModifier(event);
            break;

        case NSEventTypeLeftMouseDown:
            if (InContent(event))
                SetMouseButton(0, true);
            break;
        case NSEventTypeLeftMouseUp:
            SetMouseButton(0, false);
            break;
        case NSEventTypeRightMouseDown:
            if (InContent(event))
                SetMouseButton(1, true);
            break;
        case NSEventTypeRightMouseUp:
            SetMouseButton(1, false);
            break;
        case NSEventTypeOtherMouseDown:
            if (InContent(event))
                SetMouseButton([event buttonNumber], true);
            break;
        case NSEventTypeOtherMouseUp:
            SetMouseButton([event buttonNumber], false);
            break;

        case NSEventTypeScrollWheel:
            if (InContent(event))
                s_State->wheelDelta += static_cast<float>([event hasPreciseScrollingDeltas] ? [event scrollingDeltaY] / kPreciseScrollPerNotch : [event scrollingDeltaY]);
            break;

        default:
            break;
        }
    }

} // namespace

@interface PantheonContentView : NSView
@end

@implementation PantheonContentView

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    (void)event;
    return YES;
}

- (void)keyDown:(NSEvent*)event
{
    (void)event;
}

- (void)keyUp:(NSEvent*)event
{
    (void)event;
}

@end

@interface PantheonDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation PantheonDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    (void)sender;
    s_State->shouldClose = true;
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification
{
    (void)notification;
    UpdateSizeFromView();
}

- (void)windowDidResignKey:(NSNotification*)notification
{
    (void)notification;
    ClearHeldInput();
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    (void)sender;
    s_State->shouldClose = true;
    return NSTerminateCancel;
}

- (void)handleQuitEvent:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply
{
    (void)event;
    (void)reply;
    s_State->shouldClose = true;
}

- (void)requestQuit:(id)sender
{
    (void)sender;
    s_State->shouldClose = true;
}

@end

bool MacosPlatform::WindowOpen(const WindowDesc& desc)
{
    @autoreleasepool
    {
        m_window.width = desc.width ? desc.width : GFX_SCREEN_WIDTH;
        m_window.height = desc.height ? desc.height : GFX_SCREEN_HEIGHT;
        m_window.shouldClose = false;
        m_window.wheelDelta = 0.0f;
        m_window.charCount = 0;
        s_State = &m_window;

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        s_Delegate = [[PantheonDelegate alloc] init];
        [NSApp setDelegate:s_Delegate];

        s_AppName = [NSString stringWithUTF8String:(desc.title ? desc.title : "Pantheon")];

        NSMenu* mainMenu = [[NSMenu alloc] init];
        NSMenuItem* appItem = [[NSMenuItem alloc] init];
        [mainMenu addItem:appItem];
        NSMenu* appMenu = [[NSMenu alloc] init];
        NSMenuItem* quitItem = [[NSMenuItem alloc] initWithTitle:[@"Quit " stringByAppendingString:s_AppName] action:@selector(requestQuit:) keyEquivalent:@"q"];
        [quitItem setTarget:s_Delegate];
        [appMenu addItem:quitItem];
        [appItem setSubmenu:appMenu];
        [NSApp setMainMenu:mainMenu];

        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
        if (desc.resizable)
            style |= NSWindowStyleMaskResizable;

        const NSRect rect = NSMakeRect(0, 0, m_window.width, m_window.height);
        s_Window = [[NSWindow alloc] initWithContentRect:rect styleMask:style backing:NSBackingStoreBuffered defer:NO];
        if (!s_Window)
        {
            Engine_LogError("%s: NSWindow creation failed", GetName());
            return false;
        }

        s_ContentView = [[PantheonContentView alloc] initWithFrame:rect];
        [s_Window setContentView:s_ContentView];
        [s_Window setReleasedWhenClosed:NO];
        [s_Window setDelegate:s_Delegate];
        [s_Window setAcceptsMouseMovedEvents:YES];
        [s_Window setContentMinSize:NSMakeSize(kMinimumWidth, kMinimumHeight)];
        [s_Window setBackgroundColor:[NSColor blackColor]];
        [s_Window setTitle:s_AppName];
        [s_Window center];
        [s_Window makeKeyAndOrderFront:nil];
        [s_Window makeFirstResponder:s_ContentView];

        [NSApp finishLaunching];
        [[NSAppleEventManager sharedAppleEventManager] setEventHandler:s_Delegate andSelector:@selector(handleQuitEvent:withReplyEvent:) forEventClass:kCoreEventClass andEventID:kAEQuitApplication];
        [NSApp activateIgnoringOtherApps:YES];

        UpdateSizeFromView();
        Engine_LogInfo("%s: window %ux%u opened", GetName(), m_window.width, m_window.height);
    }
    return true;
}

void MacosPlatform::PumpMessages()
{
    @autoreleasepool
    {
        for (;;)
        {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
            if (!event)
                break;
            HandleEvent(event);
            [NSApp sendEvent:event];
        }

        if (s_Window && s_ContentView)
        {
            const NSPoint location = [s_Window mouseLocationOutsideOfEventStream];
            m_window.mouseX = static_cast<float>(location.x);
            m_window.mouseY = static_cast<float>([s_ContentView bounds].size.height - location.y);
        }
    }
}

void MacosPlatform::WindowClose()
{
    if (!s_Window)
        return;

    @autoreleasepool
    {
        [s_Window setDelegate:nil];
        [s_Window orderOut:nil];
        [s_Window close];
    }
    s_ContentView = nil;
    s_Window = nil;
    s_State = nullptr;
}

bool MacosPlatform::WindowShouldClose() const { return m_window.shouldClose; }

void MacosPlatform::GetFramebufferSize(uint32_t* outWidth, uint32_t* outHeight) const
{
    if (outWidth)
        *outWidth = m_window.width;
    if (outHeight)
        *outHeight = m_window.height;
}

void* MacosPlatform::GetNativeWindowHandle() const { return (__bridge void*)s_ContentView; }
