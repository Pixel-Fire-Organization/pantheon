#pragma once

/// Show a modal error alert. AppKit may only be called from the main thread, so any other thread gets false
/// and the caller aborts without a dialog.
/// @param title Window title of the alert.
/// @param body Message text.
/// @return True when the alert was shown and dismissed.
bool Macos_ShowPanicAlert(const char* title, const char* body);
