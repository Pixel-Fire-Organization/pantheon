#pragma once

// ---------------------------------------------------------------------------
// The open-dialog flag every PSP renderer must observe when it presents.
//
// A system dialog here is composited into the title's own back buffer rather
// than drawn over it by the system, so the flag is platform state rather than
// something a caller passes. See docs/psp/PLATFORM.md.
// ---------------------------------------------------------------------------

/// @param active True while a system dialog is open.
void PspUtilityDialog_SetActive(bool active);

/// @return True while a system dialog is open and every presented frame must
///         be handed to the dialog service.
bool PspUtilityDialog_IsActive();

/// Which dialog service the open dialog belongs to, so the present path drives
/// the right one.
enum class PspDialogService : unsigned char
{
    None = 0,
    Message,
    Osk
};

/// @param service The service owning the open dialog.
void PspUtilityDialog_SetService(PspDialogService service);

/// @return The service owning the open dialog, or None.
PspDialogService PspUtilityDialog_Service();

/// Drive whichever dialog service is open for this frame. Called by a renderer
/// between finishing its own drawing and presenting.
void PspUtilityDialog_Update();
