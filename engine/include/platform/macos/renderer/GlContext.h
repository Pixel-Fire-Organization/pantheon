#pragma once

/// Create a 4.1 core-profile OpenGL context in a child view of the window's content view, make it current on the
/// calling thread and turn vertical synchronisation on. Only one context exists at a time.
/// @return False, with the reason logged, when the platform has no window or the driver grants no 4.1 core context.
bool MacosGl_Create();

/// Present the back buffer.
void MacosGl_Swap();

/// Tell the context its drawable changed size. Call after the window is resized.
void MacosGl_Update();

/// Release the context and remove the view that hosted it. Safe to call when nothing was created.
void MacosGl_Destroy();
