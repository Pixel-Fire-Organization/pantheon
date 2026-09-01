#include "UtilityDialog.h"

extern "C" {
#include <psputility.h>
}

namespace
{
    bool s_Active = false;
    PspDialogService s_Service = PspDialogService::None;
}

void PspUtilityDialog_SetActive(bool active) { s_Active = active; }

bool PspUtilityDialog_IsActive() { return s_Active; }

void PspUtilityDialog_SetService(PspDialogService service) { s_Service = service; }

PspDialogService PspUtilityDialog_Service() { return s_Service; }

void PspUtilityDialog_Update()
{
    if (!s_Active)
        return;

    switch (s_Service)
    {
    case PspDialogService::Message:
        if (sceUtilityMsgDialogGetStatus() == PSP_UTILITY_DIALOG_VISIBLE)
            sceUtilityMsgDialogUpdate(1);
        break;
    case PspDialogService::Osk:
        if (sceUtilityOskGetStatus() == PSP_UTILITY_DIALOG_VISIBLE)
            sceUtilityOskUpdate(1);
        break;
    case PspDialogService::None:
        break;
    }
}
