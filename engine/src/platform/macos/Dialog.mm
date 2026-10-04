#import <Cocoa/Cocoa.h>

#include <cstring>
#include <pthread.h>

#include "Platform.h"
#include "platform/macos/PanicAlert.h"

namespace
{

    NSString* TextOrEmpty(const char* text) { return [NSString stringWithUTF8String:(text ? text : "")] ?: @""; }

} // namespace

bool MacosPlatform::Dialog_Open(const DialogRequest& request)
{
    if (request.kind == DialogKind::TextInput)
        return false;

    @autoreleasepool
    {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:TextOrEmpty(request.title)];
        [alert setInformativeText:TextOrEmpty(request.body)];
        [alert addButtonWithTitle:@"OK"];
        if (request.kind == DialogKind::Confirm)
            [alert addButtonWithTitle:@"Cancel"];

        const NSModalResponse response = [alert runModal];
        m_dialogResult = (request.kind == DialogKind::Confirm && response == NSAlertSecondButtonReturn) ? DialogStatus::Cancelled : DialogStatus::Accepted;
    }

    memset(m_window.keyDown, 0, sizeof(m_window.keyDown));
    memset(m_window.mouseDown, 0, sizeof(m_window.mouseDown));
    return true;
}

DialogStatus MacosPlatform::Dialog_Poll()
{
    const DialogStatus result = m_dialogResult;
    if (result == DialogStatus::Accepted || result == DialogStatus::Cancelled)
        m_dialogResult = DialogStatus::Idle;
    return result;
}

void MacosPlatform::Dialog_Cancel() { m_dialogResult = DialogStatus::Idle; }

bool Macos_ShowPanicAlert(const char* title, const char* body)
{
    if (!pthread_main_np())
        return false;

    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp activateIgnoringOtherApps:YES];

        NSAlert* alert = [[NSAlert alloc] init];
        [alert setAlertStyle:NSAlertStyleCritical];
        [alert setMessageText:TextOrEmpty(title)];
        [alert setInformativeText:TextOrEmpty(body)];
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }
    return true;
}
