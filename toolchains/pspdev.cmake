# PlayStation Portable toolchain. See docs/psp/BUILD.md.

if("$ENV{PSPDEV}" STREQUAL "")
    message(FATAL_ERROR "PSPDEV environment variable is not set!")
endif()

if(NOT EXISTS "$ENV{PSPDEV}/psp/share/pspdev.cmake")
    message(FATAL_ERROR
        "$ENV{PSPDEV}/psp/share/pspdev.cmake is missing.\n"
        "The toolchain predates the CMake support this build needs; install a current pspdev.\n"
        "See docs/psp/BUILD.md.")
endif()

include("$ENV{PSPDEV}/psp/share/pspdev.cmake")

set(ENGINE_TOOLCHAIN_ID "pspdev" CACHE INTERNAL "Active toolchain")

# Appended, not forced: the vendor file establishes the sysroot include and link
# paths through *_FLAGS_INIT, and forcing the flag variables here discards them.
# A toolchain file is re-read on every compiler probe, hence the guard.
if(NOT ENGINE_PSP_FLAGS_APPLIED)
    # -G0 is not optional: without it the compiler places small objects in a
    # global-pointer-relative section the linker script does not size for, and
    # the link fails with a relocation truncation naming an unrelated object.
    set(_PSP_COMMON_FLAGS "-O2 -Wall -G0")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_PSP_COMMON_FLAGS} -std=c99")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_PSP_COMMON_FLAGS} -std=c++11 -fno-exceptions -fno-rtti")
    set(ENGINE_PSP_FLAGS_APPLIED TRUE CACHE INTERNAL "PSP flag append guard")
endif()
