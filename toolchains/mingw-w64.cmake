# ---------------------------------------------------------------------------
# Win32 target, cross-compiled from WSL/Linux with MinGW-w64.
#
# Cross-compiling keeps one build command in one environment: the same
# `python3 tools/build.py` invocation that produces a PS2 ISO also produces a
# Windows executable, and every GCC extension the engine already uses compiles
# unchanged.
#
# Requires:  sudo apt install mingw-w64
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(ENGINE_TOOLCHAIN_ID "mingw-w64" CACHE INTERNAL "Active toolchain")

set(MINGW_TARGET x86_64-w64-mingw32)

find_program(CMAKE_C_COMPILER   ${MINGW_TARGET}-gcc     REQUIRED)
find_program(CMAKE_CXX_COMPILER ${MINGW_TARGET}-g++     REQUIRED)
find_program(CMAKE_RC_COMPILER  ${MINGW_TARGET}-windres)
find_program(CMAKE_AR           ${MINGW_TARGET}-ar      REQUIRED)
find_program(CMAKE_RANLIB       ${MINGW_TARGET}-ranlib  REQUIRED)

set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_TARGET})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# __USE_MINGW_ANSI_STDIO is not optional: MinGW's default printf is msvcrt's,
# which does not understand the %zu / %zd conversions the engine uses throughout
# its logging and would print garbage instead.
set(_MINGW_COMMON_FLAGS "-D__USE_MINGW_ANSI_STDIO=1 -O2 -Wall -g")
set(CMAKE_C_FLAGS   "${_MINGW_COMMON_FLAGS} -std=c99"                        CACHE STRING "C Flags"   FORCE)
set(CMAKE_CXX_FLAGS "${_MINGW_COMMON_FLAGS} -std=c++11 -fno-exceptions -fno-rtti" CACHE STRING "CXX Flags" FORCE)

# Static-link the GCC runtime so the .exe runs on a machine without MinGW.
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++" CACHE STRING "Exec Link Flags" FORCE)
