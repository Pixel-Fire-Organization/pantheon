#include "core/EngineDebug.h"
#include "Platform.h"

#include <switch.h>

void NxPlatform::InitApplet()
{
    const Result priority = svcSetThreadPriority(CUR_THREAD_HANDLE, PLATFORM_MAIN_THREAD_PRIORITY);
    if (R_FAILED(priority))
        Engine_LogError("%s: could not set the main thread priority (0x%X); the IO worker may not run above it", GetName(), static_cast<unsigned>(priority));

    m_exitLocked = R_SUCCEEDED(appletLockExit());
    appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
    RefreshOperationMode();
}

void NxPlatform::ShutdownApplet()
{
    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleep);
    if (m_exitLocked)
        appletUnlockExit();
    m_exitLocked = false;
}

void NxPlatform::RefreshOperationMode()
{
    const bool docked = appletGetOperationMode() == AppletOperationMode_Console;
    const bool changed = docked != m_docked;

    m_docked = docked;
    m_framebufferWidth = docked ? GFX_NX_DOCKED_WIDTH : GFX_SCREEN_WIDTH;
    m_framebufferHeight = docked ? GFX_NX_DOCKED_HEIGHT : GFX_SCREEN_HEIGHT;

    if (changed && m_initialised)
        Engine_LogInfo("%s: %s, framebuffer %ux%u", GetName(), docked ? "docked" : "handheld", m_framebufferWidth, m_framebufferHeight);
}

void NxPlatform::PumpAppletMessages()
{
    u32 message = 0;
    while (R_SUCCEEDED(appletGetMessage(&message)))
    {
        if (!appletProcessMessage(message))
            m_exitRequested = true;

        if (message == AppletMessage_OperationModeChanged)
        {
            RefreshOperationMode();
        }
        else if (message == AppletMessage_FocusStateChanged)
        {
            const bool background = appletGetFocusState() == AppletFocusState_Background;
            if (background && !m_suspended)
            {
                m_suspendStartTick = armGetSystemTick();
                m_suspended = true;
                appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);
            }
            else if (!background && m_suspended)
            {
                m_suspendedTicks += armGetSystemTick() - m_suspendStartTick;
                m_suspended = false;
                appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
            }
        }
    }
}
