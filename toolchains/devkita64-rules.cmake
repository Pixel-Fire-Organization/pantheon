# Rules override for toolchains/devkita64.cmake. CMake loads this after
# devkitPro's platform file has set the default flags and before those defaults
# seed the cache, once per enabled language. See docs/nx/BUILD.md.

include("$ENV{DEVKITPRO}/cmake/dkp-rule-overrides.cmake")

# Appended once per configure: the platform file sets the defaults for every
# language at once and does not reset them between languages.
if(NOT ENGINE_NX_FLAGS_APPENDED)
    # The toolchain include roots are system directories so -Werror applies to
    # this project's code and not to warnings inside the vendor headers.
    set(_ENGINE_NX_COMMON_FLAGS "-O2 -Wall -isystem $ENV{DEVKITPRO}/libnx/include -isystem $ENV{DEVKITPRO}/portlibs/switch/include")
    string(APPEND CMAKE_C_FLAGS_INIT " ${_ENGINE_NX_COMMON_FLAGS} -std=c99")
    string(APPEND CMAKE_CXX_FLAGS_INIT " ${_ENGINE_NX_COMMON_FLAGS} -std=c++11 -fno-exceptions -fno-rtti")
    set(ENGINE_NX_FLAGS_APPENDED TRUE)
endif()
