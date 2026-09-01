#include "Platform.h"

#include "platform/PlatformRegistry.h"

// This platform is the one compiled into the binary. Engine_Main calls
// Platform_CreateBuiltin() directly, which is what guarantees this object is
// pulled out of the static library at link time.
PLATFORM_DEFINE_BUILTIN(PlatformId::Ps2Pal, "ps2pal", Ps2PalPlatform)
