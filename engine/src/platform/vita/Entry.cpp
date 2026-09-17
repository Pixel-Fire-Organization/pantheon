#include "PlatformConstants.h"
#include "core/EngineMain.h"

extern "C" {
#include <psp2/kernel/processmgr.h>

unsigned int _newlib_heap_size_user = MEM_HEAP_SIZE;
}

int main(int argc, char** argv)
{
    const int rc = Engine_Main(argc, argv);
    sceKernelExitProcess(rc);
    return rc;
}
