# ---------------------------------------------------------------------------
# Platform registry and selection.
#
# PLATFORMS_TO_SUPPORT lists which platforms this configure builds. It defaults
# to every known platform and is then filtered against the active toolchain, so
# the common case needs no flag:
#
#   cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/ps2dev.cmake    -B build/ps2
#   cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/mingw-w64.cmake -B build/win32
#   cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/vitasdk.cmake   -B build/vita
#
# Each surviving platform gets its own engine library, its own executable, and
# its own self-contained dist/<name>/ bundle. Nothing is shared between bundles,
# so a PAL disc image can never be handed to a Win32 build.
# ---------------------------------------------------------------------------

set(ENGINE_KNOWN_PLATFORMS PS2PAL PS2NTSC WIN32 VITA VITATV PSP NX)

# Per-platform metadata. Keep these together: adding a platform should be one
# block here plus one <dir> subdirectory under each of engine/include/platform/,
# engine/src/platform/ and engine/config/.
#   _DIR        subdirectory (under each of the three roots above) holding the concrete platform
#   _BASE       shared base directory (empty when the platform has no base)
#   _TOOLCHAIN  ENGINE_TOOLCHAIN_ID this platform requires, set by toolchains/*.cmake
#   _EXE        executable file name
#   _DIST       dist/<name>/ bundle directory
set(PLATFORM_PS2PAL_DIR        "ps2/pal")
set(PLATFORM_PS2PAL_BASE       "ps2")
set(PLATFORM_PS2PAL_TOOLCHAIN  "ps2dev")
set(PLATFORM_PS2PAL_EXE        "main.elf")
set(PLATFORM_PS2PAL_DIST       "ps2pal")

set(PLATFORM_PS2NTSC_DIR       "ps2/ntsc")
set(PLATFORM_PS2NTSC_BASE      "ps2")
set(PLATFORM_PS2NTSC_TOOLCHAIN "ps2dev")
set(PLATFORM_PS2NTSC_EXE       "main.elf")
set(PLATFORM_PS2NTSC_DIST      "ps2ntsc")

set(PLATFORM_WIN32_DIR         "win32")
set(PLATFORM_WIN32_BASE        "")
set(PLATFORM_WIN32_TOOLCHAIN   "mingw-w64")
set(PLATFORM_WIN32_EXE         "game.exe")
set(PLATFORM_WIN32_DIST        "win32")

set(PLATFORM_VITA_DIR          "vita/handheld")
set(PLATFORM_VITA_BASE         "vita")
set(PLATFORM_VITA_TOOLCHAIN    "vitasdk")
set(PLATFORM_VITA_EXE          "main.elf")
set(PLATFORM_VITA_DIST         "vita")

set(PLATFORM_VITATV_DIR        "vita/tv")
set(PLATFORM_VITATV_BASE       "vita")
set(PLATFORM_VITATV_TOOLCHAIN  "vitasdk")
set(PLATFORM_VITATV_EXE        "main.elf")
set(PLATFORM_VITATV_DIST       "vitatv")

set(PLATFORM_PSP_DIR           "psp")
set(PLATFORM_PSP_BASE          "")
set(PLATFORM_PSP_TOOLCHAIN     "pspdev")
set(PLATFORM_PSP_EXE           "main.elf")
set(PLATFORM_PSP_DIST          "psp")

set(PLATFORM_NX_DIR            "nx")
set(PLATFORM_NX_BASE           "")
set(PLATFORM_NX_TOOLCHAIN      "devkita64")
set(PLATFORM_NX_EXE            "main.elf")
set(PLATFORM_NX_DIST           "nx")

set(PLATFORMS_TO_SUPPORT "${ENGINE_KNOWN_PLATFORMS}"
    CACHE STRING "Platforms to build. Defaults to every known platform, filtered by the active toolchain.")

# Did the user name platforms explicitly, or are we filtering the default list?
# An explicit list naming an incompatible platform is a user error; the default
# list fanning out across toolchains is not.
set(_EXPLICIT_LIST TRUE)
if("${PLATFORMS_TO_SUPPORT}" STREQUAL "${ENGINE_KNOWN_PLATFORMS}")
    set(_EXPLICIT_LIST FALSE)
endif()

set(ENGINE_ACTIVE_PLATFORMS "")
set(_SKIPPED "")

foreach(_P ${PLATFORMS_TO_SUPPORT})
    string(TOUPPER "${_P}" _P)

    if(NOT _P IN_LIST ENGINE_KNOWN_PLATFORMS)
        message(FATAL_ERROR
            "Unknown platform '${_P}'.\n"
            "Known platforms: ${ENGINE_KNOWN_PLATFORMS}")
    endif()

    set(_DIR "${CMAKE_SOURCE_DIR}/engine/config/${PLATFORM_${_P}_DIR}")
    set(_CMAKE_FRAGMENT "${CMAKE_SOURCE_DIR}/engine/config/${PLATFORM_${_P}_BASE}/platform.cmake")
    if(PLATFORM_${_P}_BASE STREQUAL "")
        set(_CMAKE_FRAGMENT "${_DIR}/platform.cmake")
    endif()

    # A platform whose sources do not exist yet is skipped, never a hard error:
    # the known-platform list is deliberately ahead of the implementation.
    if(NOT EXISTS "${_CMAKE_FRAGMENT}")
        list(APPEND _SKIPPED "${_P} (not implemented yet)")
        continue()
    endif()

    if(NOT ENGINE_TOOLCHAIN_ID STREQUAL PLATFORM_${_P}_TOOLCHAIN)
        if(_EXPLICIT_LIST)
            message(FATAL_ERROR
                "Platform '${_P}' needs toolchains/${PLATFORM_${_P}_TOOLCHAIN}.cmake, "
                "but this configure used '${ENGINE_TOOLCHAIN_ID}'.\n"
                "Use the matching toolchain file in toolchains/, or drop '${_P}' from PLATFORMS_TO_SUPPORT.")
        endif()
        list(APPEND _SKIPPED "${_P} (needs the ${PLATFORM_${_P}_TOOLCHAIN} toolchain)")
        continue()
    endif()

    list(APPEND ENGINE_ACTIVE_PLATFORMS "${_P}")
endforeach()

if(NOT ENGINE_ACTIVE_PLATFORMS)
    message(FATAL_ERROR
        "No platform survives this configuration.\n"
        "Requested: ${PLATFORMS_TO_SUPPORT}\n"
        "Toolchain reports ENGINE_TOOLCHAIN_ID='${ENGINE_TOOLCHAIN_ID}'.\n"
        "Pick a toolchain from toolchains/ that matches one of: ${ENGINE_KNOWN_PLATFORMS}")
endif()

message(STATUS "Platforms: ${ENGINE_ACTIVE_PLATFORMS}")
if(_SKIPPED)
    message(STATUS "  skipped: ${_SKIPPED}")
endif()

# The first surviving platform is the default for anything that needs to pick
# one (e.g. which ISO the default run target launches).
list(GET ENGINE_ACTIVE_PLATFORMS 0 ENGINE_DEFAULT_PLATFORM)

# Absolute paths a platform's headers and sources live at: variant then base,
# on the include side and the src side. A platform with no base (Win32) gets
# an empty base pair, exactly as before the include/src split.
function(engine_platform_dirs PLATFORM OUT_VARIANT_INCLUDE_DIR OUT_BASE_INCLUDE_DIR OUT_VARIANT_SRC_DIR OUT_BASE_SRC_DIR)
    set(${OUT_VARIANT_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/engine/include/platform/${PLATFORM_${PLATFORM}_DIR}" PARENT_SCOPE)
    set(${OUT_VARIANT_SRC_DIR} "${CMAKE_SOURCE_DIR}/engine/src/platform/${PLATFORM_${PLATFORM}_DIR}" PARENT_SCOPE)
    if(PLATFORM_${PLATFORM}_BASE STREQUAL "")
        set(${OUT_BASE_INCLUDE_DIR} "" PARENT_SCOPE)
        set(${OUT_BASE_SRC_DIR} "" PARENT_SCOPE)
    else()
        set(${OUT_BASE_INCLUDE_DIR} "${CMAKE_SOURCE_DIR}/engine/include/platform/${PLATFORM_${PLATFORM}_BASE}" PARENT_SCOPE)
        set(${OUT_BASE_SRC_DIR} "${CMAKE_SOURCE_DIR}/engine/src/platform/${PLATFORM_${PLATFORM}_BASE}" PARENT_SCOPE)
    endif()
endfunction()
