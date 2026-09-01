#include "CommonDialog.h"

namespace
{
    bool s_Active = false;
    int s_LastResult = 0;
}

void VitaCommonDialog_SetActive(bool active) { s_Active = active; }

bool VitaCommonDialog_IsActive() { return s_Active; }

void VitaCommonDialog_SetLastResult(int rc) { s_LastResult = rc; }

int VitaCommonDialog_LastResult() { return s_LastResult; }
