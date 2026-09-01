# ---------------------------------------------------------------------------
# Win32 platform fragment.
#
# Included by cmake/Platforms.cmake only when WIN32 survives selection, so a PS2
# build never evaluates any of this.
#
# Cross-compiled from WSL with MinGW-w64 (toolchains/mingw-w64.cmake).
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# wgpu-native (WebGPU backend).
#
# A pinned PREBUILT release, fetched at configure time and SHA256-checked, not a
# submodule and not committed: it is a 65 MB binary artifact, not source, and
# Dawn is not buildable under MinGW. The x86_64-pc-windows-gnu build is the one
# that links against this toolchain.
#
# The DLL is linked rather than the 36 MB static library, and copied into the
# bundle - a 14 MB DLL beside a small exe beats a 37 MB exe.
# ---------------------------------------------------------------------------
set(WGPU_NATIVE_VERSION "v29.0.1.1" CACHE STRING "Pinned wgpu-native release")
set(WGPU_NATIVE_SHA256 "d471e3614733c1d4ddd61bfd19868356477d0d37bf531bf8c6cb64a7f579bd2a")
set(WGPU_NATIVE_DIR "${CMAKE_BINARY_DIR}/_deps/wgpu-native")

function(win32_fetch_wgpu_native)
    if(EXISTS "${WGPU_NATIVE_DIR}/include/webgpu/webgpu.h")
        return()
    endif()

    set(_url "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_NATIVE_VERSION}/wgpu-windows-x86_64-gnu-release.zip")
    set(_zip "${WGPU_NATIVE_DIR}/wgpu.zip")

    message(STATUS "Fetching wgpu-native ${WGPU_NATIVE_VERSION}...")
    file(MAKE_DIRECTORY "${WGPU_NATIVE_DIR}")
    file(DOWNLOAD "${_url}" "${_zip}"
         EXPECTED_HASH SHA256=${WGPU_NATIVE_SHA256}
         SHOW_PROGRESS
         STATUS _status)

    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
        list(GET _status 1 _msg)
        message(FATAL_ERROR
            "Could not fetch wgpu-native ${WGPU_NATIVE_VERSION}: ${_msg}
"
            "The WebGPU backend needs it. Build with -DPLATFORMS_TO_SUPPORT=WIN32 and
"
            "--renderer opengl to skip it, or place the unpacked release at
"
            "${WGPU_NATIVE_DIR}")
    endif()

    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${_zip}" WORKING_DIRECTORY "${WGPU_NATIVE_DIR}")
endfunction()

function(platform_configure PLATFORM)
    win32_fetch_wgpu_native()

    engine_platform_dirs(${PLATFORM} _variantIncludeDir _baseIncludeDir _variantSrcDir _baseSrcDir)

    set(ENGINE_PLATFORM_${PLATFORM}_SOURCES
        "${_variantSrcDir}/Platform.cpp"
        "${_variantSrcDir}/Entry.cpp"
        "${_variantSrcDir}/Memory.cpp"
        "${_variantSrcDir}/Time.cpp"
        "${_variantSrcDir}/Thread.cpp"
        "${_variantSrcDir}/Console.cpp"
        "${_variantSrcDir}/Filesystem.cpp"
        "${_variantSrcDir}/Input.cpp"
        "${_variantSrcDir}/Window.cpp"
        "${_variantIncludeDir}/Platform.h"
        "${_variantIncludeDir}/PlatformConstants.h"
        "${_variantSrcDir}/renderer/GlApi.cpp"
        "${_variantIncludeDir}/renderer/GlApi.h"
        "${_variantSrcDir}/renderer/OpenGl.cpp"
        "${_variantIncludeDir}/renderer/OpenGl.h"
        "${_variantSrcDir}/renderer/WebGpu.cpp"
        "${_variantIncludeDir}/renderer/WebGpu.h"
        PARENT_SCOPE)

    set(ENGINE_PLATFORM_${PLATFORM}_INCLUDES "${WGPU_NATIVE_DIR}/include" PARENT_SCOPE)

    # xinput9_1_0  gamepads (present on Vista onward)
    # user32/gdi32  window, GetAsyncKeyState, GetCursorPos, MessageBox
    # kernel32      threads, semaphores, QueryPerformanceCounter, console
    # dbghelp       panic stack symbolisation and minidump writing
    # opengl32      WGL context creation plus the GL 1.1 entry points
    # wgpu_native   linked through its import library (see above)
    set(ENGINE_PLATFORM_${PLATFORM}_LIBS
        "${WGPU_NATIVE_DIR}/lib/libwgpu_native.dll.a"
        opengl32 xinput9_1_0 user32 gdi32 kernel32 dbghelp
        PARENT_SCOPE)
endfunction()

# Stage a self-contained bundle: the exe plus the assets it needs, so the folder
# can be copied anywhere and run. Deliberately not an installer - dist/win32/ is
# the deliverable.
# Launch the staged executable.
function(platform_run PLATFORM EXE_TARGET DIST_DIR)
    add_custom_target(run-${PLATFORM_${PLATFORM}_DIST}
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/run_target.py" "${DIST_DIR}/${PLATFORM_${PLATFORM}_EXE}"
        COMMENT "Launching dist/${PLATFORM_${PLATFORM}_DIST}/${PLATFORM_${PLATFORM}_EXE}"
        VERBATIM
    )
endfunction()

function(platform_package PLATFORM EXE_TARGET DIST_DIR)
    string(TOLOWER "${PLATFORM}" _lower)

    add_custom_target(bundle-${PLATFORM_${PLATFORM}_DIST}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/pack_master_archive.py"
                --rassets "${CMAKE_SOURCE_DIR}/dist/cooked/${_lower}/rassets"
                --prefix RASSETS
                --levels "${CMAKE_SOURCE_DIR}/dist/cooked/${_lower}/levels"
                --dst "${DIST_DIR}/RASSETS.PS2R"
        # The bundle must run from a copied folder, so the DLL ships beside the exe.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${WGPU_NATIVE_DIR}/lib/wgpu_native.dll" "${DIST_DIR}/wgpu_native.dll"
        COMMENT "Staging dist/${PLATFORM_${PLATFORM}_DIST}/ (exe + assets)"
        VERBATIM
    )
    add_dependencies(bundle-${PLATFORM_${PLATFORM}_DIST} ${EXE_TARGET} package-${_lower})
    set(PLATFORM_${PLATFORM}_PACKAGE_TARGET "bundle-${PLATFORM_${PLATFORM}_DIST}" PARENT_SCOPE)

endfunction()
