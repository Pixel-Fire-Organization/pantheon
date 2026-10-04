# ---------------------------------------------------------------------------
# macOS platform fragment.
#
# Included by cmake/Platforms.cmake only when MACOS survives selection, so no
# other platform's configure evaluates any of this - not the Objective-C++
# language, not the wgpu-native fetch, not the frameworks.
#
# Built natively with Apple's clang (toolchains/macos.cmake).
# ---------------------------------------------------------------------------
if(CMAKE_VERSION VERSION_LESS 3.19)
    message(FATAL_ERROR "The macOS platform needs CMake 3.19 or newer (JSON parsing and Objective-C++); this is ${CMAKE_VERSION}.")
endif()

enable_language(OBJCXX)

# ---------------------------------------------------------------------------
# wgpu-native (WebGPU backend, Metal).
#
# A pinned PREBUILT release per architecture, fetched at configure time and
# SHA256-checked, then merged into one universal library: upstream publishes a
# separate archive for each. The prebuilt carries the Metal backend only - no
# OpenGL and no Vulkan - which is why the fallback renderer is native OpenGL
# rather than wgpu's own. See docs/macos/renderers/WEBGPU.md.
#
# The dynamic library is linked and shipped inside the bundle, the same choice
# the Win32 platform makes for its DLL.
# ---------------------------------------------------------------------------
set(WGPU_NATIVE_VERSION "v29.0.1.1" CACHE STRING "Pinned wgpu-native release")
set(WGPU_NATIVE_SHA256_X86_64 "8e2f7378548ddd0e2cf21e7d864dda46e953f0af724855a33778b85ead206d41")
set(WGPU_NATIVE_SHA256_ARM64 "a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e")
set(WGPU_NATIVE_DIR "${CMAKE_BINARY_DIR}/_deps/wgpu-native")
set(WGPU_NATIVE_UNIVERSAL_DIR "${WGPU_NATIVE_DIR}/universal")

function(macos_fetch_wgpu_native_arch ARCH OUT_DYLIB)
    if(ARCH STREQUAL "x86_64")
        set(_asset "x86_64")
        set(_hash "${WGPU_NATIVE_SHA256_X86_64}")
    elseif(ARCH STREQUAL "arm64")
        set(_asset "aarch64")
        set(_hash "${WGPU_NATIVE_SHA256_ARM64}")
    else()
        message(FATAL_ERROR "macOS architecture '${ARCH}' has no wgpu-native build. Use arm64, x86_64 or both.")
    endif()

    set(_dir "${WGPU_NATIVE_DIR}/${ARCH}")
    if(NOT EXISTS "${_dir}/lib/libwgpu_native.dylib")
        set(_url "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_NATIVE_VERSION}/wgpu-macos-${_asset}-release.zip")
        set(_zip "${_dir}/wgpu.zip")

        message(STATUS "Fetching wgpu-native ${WGPU_NATIVE_VERSION} (${ARCH})...")
        file(MAKE_DIRECTORY "${_dir}")
        file(DOWNLOAD "${_url}" "${_zip}"
             EXPECTED_HASH SHA256=${_hash}
             SHOW_PROGRESS
             STATUS _status)

        list(GET _status 0 _code)
        if(NOT _code EQUAL 0)
            list(GET _status 1 _msg)
            message(FATAL_ERROR
                "Could not fetch wgpu-native ${WGPU_NATIVE_VERSION} for ${ARCH}: ${_msg}\n"
                "The WebGPU backend needs it. Place the unpacked release at ${_dir}")
        endif()

        execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${_zip}" WORKING_DIRECTORY "${_dir}")
    endif()

    set(${OUT_DYLIB} "${_dir}/lib/libwgpu_native.dylib" PARENT_SCOPE)
endfunction()

function(macos_fetch_wgpu_native)
    if(EXISTS "${WGPU_NATIVE_UNIVERSAL_DIR}/lib/libwgpu_native.dylib" AND EXISTS "${WGPU_NATIVE_UNIVERSAL_DIR}/include/webgpu/webgpu.h")
        return()
    endif()

    set(_dylibs "")
    foreach(_arch ${CMAKE_OSX_ARCHITECTURES})
        macos_fetch_wgpu_native_arch(${_arch} _dylib)
        list(APPEND _dylibs "${_dylib}")
    endforeach()
    list(GET CMAKE_OSX_ARCHITECTURES 0 _first)

    file(MAKE_DIRECTORY "${WGPU_NATIVE_UNIVERSAL_DIR}/lib")
    execute_process(
        COMMAND lipo -create ${_dylibs} -output "${WGPU_NATIVE_UNIVERSAL_DIR}/lib/libwgpu_native.dylib"
        RESULT_VARIABLE _lipo)
    if(NOT _lipo EQUAL 0)
        message(FATAL_ERROR "lipo could not merge the wgpu-native libraries: ${_dylibs}")
    endif()
    file(COPY "${WGPU_NATIVE_DIR}/${_first}/include" DESTINATION "${WGPU_NATIVE_UNIVERSAL_DIR}")
endfunction()

function(platform_configure PLATFORM)
    macos_fetch_wgpu_native()

    engine_platform_dirs(${PLATFORM} _variantIncludeDir _baseIncludeDir _variantSrcDir _baseSrcDir)

    set(ENGINE_PLATFORM_${PLATFORM}_SOURCES
        "${_variantSrcDir}/Platform.cpp"
        "${_variantSrcDir}/Entry.cpp"
        "${_variantSrcDir}/Memory.cpp"
        "${_variantSrcDir}/Time.cpp"
        "${_variantSrcDir}/Thread.cpp"
        "${_variantSrcDir}/Console.cpp"
        "${_variantSrcDir}/Filesystem.cpp"
        "${_variantSrcDir}/Input.mm"
        "${_variantSrcDir}/Window.mm"
        "${_variantSrcDir}/Dialog.mm"
        "${_variantIncludeDir}/Platform.h"
        "${_variantIncludeDir}/PlatformConstants.h"
        "${_variantIncludeDir}/PanicAlert.h"
        "${_variantSrcDir}/renderer/MacosWebGpuRenderer.mm"
        "${_variantIncludeDir}/renderer/MacosWebGpuRenderer.h"
        "${_variantSrcDir}/renderer/OpenGl.cpp"
        "${_variantIncludeDir}/renderer/OpenGl.h"
        "${_variantSrcDir}/renderer/GlContext.mm"
        "${_variantIncludeDir}/renderer/GlContext.h"
        "${CMAKE_SOURCE_DIR}/engine/src/graphics/webgpu/WebGpuRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/engine/include/graphics/webgpu/WebGpuRenderer.h"
        "${CMAKE_SOURCE_DIR}/engine/src/graphics/ShaderAssets.cpp"
        "${CMAKE_SOURCE_DIR}/engine/include/graphics/ShaderAssets.h"
        PARENT_SCOPE)

    set(ENGINE_PLATFORM_${PLATFORM}_INCLUDES "${WGPU_NATIVE_UNIVERSAL_DIR}/include" PARENT_SCOPE)

    # OpenGL is deprecated on macOS, and the build treats warnings as errors.
    set(ENGINE_PLATFORM_${PLATFORM}_DEFINES GL_SILENCE_DEPRECATION PARENT_SCOPE)

    # Cocoa         window, events, alerts
    # Metal         the surface the WebGPU backend presents through
    # QuartzCore    the Metal layer type
    # GameController  controllers
    # OpenGL        the fallback renderer
    # wgpu_native   linked dynamically and shipped in the bundle's Frameworks
    set(ENGINE_PLATFORM_${PLATFORM}_LIBS
        "${WGPU_NATIVE_UNIVERSAL_DIR}/lib/libwgpu_native.dylib"
        "-framework Cocoa"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework GameController"
        "-framework OpenGL"
        "-Wl,-rpath,@executable_path/../Frameworks"
        PARENT_SCOPE)

    # Actions are resolved against the pad, keyboard and mouse this platform reports.
    set(ACTIONS_CAPS_macos "${CMAKE_SOURCE_DIR}/engine/config/macos/input_capabilities.json" CACHE INTERNAL "")
endfunction()

# Launch the staged executable inside its bundle, so the data root resolves the
# way it does for a player and the console log stays on the terminal.
function(platform_run PLATFORM EXE_TARGET DIST_DIR)
    add_custom_target(run-${PLATFORM_${PLATFORM}_DIST}
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/run_target.py" --launcher native
                "${DIST_DIR}/${MACOS_BUNDLE_NAME}.app/Contents/MacOS/${PLATFORM_${PLATFORM}_EXE}"
        COMMENT "Launching dist/${PLATFORM_${PLATFORM}_DIST}/${MACOS_BUNDLE_NAME}.app"
        VERBATIM
    )
endfunction()

# The bundle is named for the title, like every other platform's package identity.
file(READ "${CMAKE_SOURCE_DIR}/game/config/title.json" _macos_title_json)
string(JSON MACOS_BUNDLE_NAME ERROR_VARIABLE _macos_title_error GET "${_macos_title_json}" name)
if(_macos_title_error OR MACOS_BUNDLE_NAME STREQUAL "")
    message(FATAL_ERROR "game/config/title.json has no 'name' to name the application bundle after: ${_macos_title_error}")
endif()

function(platform_package PLATFORM EXE_TARGET DIST_DIR)
    string(TOLOWER "${PLATFORM}" _lower)

    add_custom_target(bundle-${PLATFORM_${PLATFORM}_DIST}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/macos_package.py"
                --exe "${DIST_DIR}/${PLATFORM_${PLATFORM}_EXE}"
                --dylib "${WGPU_NATIVE_UNIVERSAL_DIR}/lib/libwgpu_native.dylib"
                --rassets "${CMAKE_SOURCE_DIR}/dist/cooked/${_lower}/rassets"
                --levels "${CMAKE_SOURCE_DIR}/dist/cooked/${_lower}/levels"
                --shaders "${CMAKE_SOURCE_DIR}/dist/cooked/${_lower}/shaders"
                --bundle "${DIST_DIR}/${MACOS_BUNDLE_NAME}.app"
        COMMENT "Staging dist/${PLATFORM_${PLATFORM}_DIST}/${MACOS_BUNDLE_NAME}.app"
        VERBATIM
    )
    add_dependencies(bundle-${PLATFORM_${PLATFORM}_DIST} ${EXE_TARGET} package-${_lower})
    set(PLATFORM_${PLATFORM}_PACKAGE_TARGET "bundle-${PLATFORM_${PLATFORM}_DIST}" PARENT_SCOPE)
endfunction()

# Examples stage the same bundle shape, named for the example, beside the
# archive the shared staging step already wrote.
function(platform_example_package PLATFORM NAME STAGE_TARGET EXE_TARGET DIST_DIR)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_cooked "${CMAKE_SOURCE_DIR}/examples/dist/${NAME}/${_lower}/cooked")

    add_custom_command(TARGET ${STAGE_TARGET} POST_BUILD
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/macos_package.py"
                --exe "${DIST_DIR}/${PLATFORM_${PLATFORM}_EXE}"
                --dylib "${WGPU_NATIVE_UNIVERSAL_DIR}/lib/libwgpu_native.dylib"
                --archive "${DIST_DIR}/RASSETS.PS2R"
                --shaders "${_cooked}/shaders"
                --bundle "${DIST_DIR}/${NAME}.app"
                --name "${NAME}"
        VERBATIM
    )
endfunction()
