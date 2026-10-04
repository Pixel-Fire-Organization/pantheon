# ---------------------------------------------------------------------------
# macOS target, built natively on a Mac with Apple's clang.
#
# There is nothing to cross-compile, so no CMAKE_SYSTEM_NAME is set. The toolchain
# exists to state the one thing the platform filter needs - ENGINE_TOOLCHAIN_ID -
# and the flags every compile of this target shares, in the same place the other
# targets state theirs.
#
# One binary carries both Intel and Apple Silicon code. MACOS_ARCHITECTURES
# narrows that: "native" builds only the host's architecture, which halves the
# compile time of a development build. It can also come from the environment of
# the same name, which is how tools/build.py is asked for one.
#
# Requires:  Xcode or the Command Line Tools, CMake 3.19 or newer
# ---------------------------------------------------------------------------

set(ENGINE_TOOLCHAIN_ID "macos" CACHE INTERNAL "Active toolchain")

# CMake includes this file again inside the projects it builds to test the compiler,
# and tells those the architectures explicitly. They cannot see the cache variable
# that chose them, so the architectures are only derived where it is defined: a
# first configure picks the default, a reconfigure finds its own earlier choice, and
# an architecture list handed straight to CMAKE_OSX_ARCHITECTURES is left alone.
if(NOT DEFINED MACOS_ARCHITECTURES AND DEFINED ENV{MACOS_ARCHITECTURES})
    set(MACOS_ARCHITECTURES "$ENV{MACOS_ARCHITECTURES}" CACHE STRING "Architectures to build: a list of arm64 and x86_64, or 'native' for the host's own")
endif()
if(NOT DEFINED MACOS_ARCHITECTURES AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(MACOS_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures to build: a list of arm64 and x86_64, or 'native' for the host's own")
endif()
if(DEFINED MACOS_ARCHITECTURES)
    if(MACOS_ARCHITECTURES STREQUAL "native")
        set(_MACOS_ARCHS "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    else()
        set(_MACOS_ARCHS "${MACOS_ARCHITECTURES}")
    endif()
    set(CMAKE_OSX_ARCHITECTURES "${_MACOS_ARCHS}" CACHE STRING "Architectures" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

set(_MACOS_COMMON_FLAGS "-O2 -Wall -g")
set(CMAKE_C_FLAGS     "${_MACOS_COMMON_FLAGS} -std=c99"                                    CACHE STRING "C Flags"     FORCE)
set(CMAKE_CXX_FLAGS   "${_MACOS_COMMON_FLAGS} -std=c++11 -fno-exceptions -fno-rtti"        CACHE STRING "CXX Flags"   FORCE)
set(CMAKE_OBJCXX_FLAGS "${_MACOS_COMMON_FLAGS} -std=c++11 -fno-exceptions -fno-rtti -fobjc-arc" CACHE STRING "OBJCXX Flags" FORCE)
