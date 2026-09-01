#pragma once

#include <cstdint>

#include "Platform.h"
#include "PlatformKeys.h"

#define PLATFORM_MAX_REGISTERED 8

typedef Platform* (*PlatformFactory)();

class PlatformRegistry final
{
public:
    // Called from static initialisers. Returns false (and logs) when the table
    // is full or the id is already taken.
    static bool Register(PlatformId id, const char* name, PlatformFactory factory);

    // Instantiate a registered platform. Instances are statically stored and
    // owned by their factory, so there is nothing to free.
    static Platform* Create(PlatformId id);

    // Case-insensitive name lookup, e.g. "ps2ntsc". Returns PlatformId::Unknown
    // when this binary does not contain that platform.
    static PlatformId FindByName(const char* name);

    static uint32_t GetCount();
    static PlatformId GetIdAt(uint32_t index);
    static const char* GetNameAt(uint32_t index);

    // Log every compiled-in platform. Used by --help and by the error path when
    // a requested platform is not present.
    static void LogRegistered();
};

// Create the platform compiled into this binary.
//
// DEFINED BY the concrete platform via PLATFORM_DEFINE_BUILTIN below, and called
// directly by Engine_Main. That direct call is the point: the platform lives in
// a static library, and a linker only extracts an archive member that resolves
// an undefined symbol. Relying on a static initialiser to self-register would
// leave that object unreferenced, silently dropped, and the registry empty.
Platform* Platform_CreateBuiltin();

// Define a concrete platform as the one this binary contains.
// Place once, at file scope, in the platform's Platform.cpp:
//     PLATFORM_DEFINE_BUILTIN(PlatformId::Ps2Pal, "ps2pal", Ps2PalPlatform)
#define PLATFORM_DEFINE_BUILTIN(idValue, nameLiteral, TypeName)                                                                                                                                        \
    static Platform* TypeName##_Create()                                                                                                                                                               \
    {                                                                                                                                                                                                  \
        static TypeName s_Instance;                                                                                                                                                                    \
        return &s_Instance;                                                                                                                                                                            \
    }                                                                                                                                                                                                  \
    Platform* Platform_CreateBuiltin()                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        static const bool s_Registered = PlatformRegistry::Register(idValue, nameLiteral, &TypeName##_Create);                                                                                         \
        (void)s_Registered;                                                                                                                                                                            \
        return TypeName##_Create();                                                                                                                                                                    \
    }
