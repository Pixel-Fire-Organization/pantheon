#pragma once

/// Indicates that a variable is unused. Silences warnings.
#define UNUSED_VAR(x) ((void)(x))

/// Evaluates to 1 if x is a non-zero power of two, 0 otherwise.
/// Only correct for integer types; do not use with signed values or 0.
#define IS_POWER_OF_TWO(x) (((x) != 0u) && (((x) & ((x) - 1u)) == 0u))

/// Silences -Wreturn-type / -Werror on stub functions.
/// Logs the unimplemented function name at runtime and returns `ret`.
/// Usage: bool Foo(void) { ENGINE_NOT_IMPLEMENTED(false); }
/// NOTE: For lua_CFunction stubs return int and push a value — write those inline.
#define ENGINE_NOT_IMPLEMENTED(ret)                                                                                                                                                                    \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        Engine_LogError("Not implemented: %s", __func__);                                                                                                                                              \
        return (ret);                                                                                                                                                                                  \
    }                                                                                                                                                                                                  \
    while (0)
