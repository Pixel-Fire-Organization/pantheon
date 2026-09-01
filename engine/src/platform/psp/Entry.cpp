#include "core/EngineMain.h"

#include "Exit.h"
#include "PlatformConstants.h"

extern "C" {
#include <pspkernel.h>
}

PSP_MODULE_INFO("ENGINE", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_PRIORITY(PLATFORM_MAIN_THREAD_PRIORITY);
PSP_HEAP_SIZE_KB(MEM_HEAP_SIZE_KB);

int main(int argc, char** argv)
{
    PspExit_Install();
    const int rc = Engine_Main(argc, argv);
    PspExit_Finish(rc);
}
