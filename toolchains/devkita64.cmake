# Nintendo Switch (nx) toolchain: devkitPro's devkitA64 with libnx. See docs/nx/BUILD.md.

if("$ENV{DEVKITPRO}" STREQUAL "")
    message(FATAL_ERROR "DEVKITPRO environment variable is not set!")
endif()

if(NOT EXISTS "$ENV{DEVKITPRO}/cmake/Switch.cmake")
    message(FATAL_ERROR
        "$ENV{DEVKITPRO}/cmake/Switch.cmake is missing.\n"
        "Install the toolchain with: dkp-pacman -S switch-dev switch-mesa switch-glad\n"
        "See docs/nx/BUILD.md.")
endif()

# devkitPro sets its architecture flags from its platform file, after this
# toolchain file has run, so this project's flags are appended from a rules
# override instead - the one hook CMake loads between the two. devkitPro only
# installs its own override when none is set; ours includes it first.
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/devkita64-rules.cmake")

include("$ENV{DEVKITPRO}/cmake/Switch.cmake")

set(ENGINE_TOOLCHAIN_ID "devkita64" CACHE INTERNAL "Active toolchain")
