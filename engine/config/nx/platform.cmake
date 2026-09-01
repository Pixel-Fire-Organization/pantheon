# ---------------------------------------------------------------------------
# Nintendo Switch (nx) platform fragment. See docs/nx/BUILD.md.
#
# There is deliberately no platform_dependencies(): both renderers are served by
# libraries the toolchain distributes, so this platform vendors nothing.
#
# devkitPro's CMake owns every variable beginning NX_; this fragment's own use
# ENGINE_NX_ so it never shadows one of them.
# ---------------------------------------------------------------------------

set(ENGINE_NX_CONFIG_DIR "${CMAKE_SOURCE_DIR}/engine/config/nx")
set(ENGINE_NX_SHADER_TOOL "${CMAKE_SOURCE_DIR}/tools/nx_shader.py")
set(ENGINE_NX_PACKAGE_TOOL "${CMAKE_SOURCE_DIR}/tools/nx_package.py")
set(ENGINE_NX_PACKAGE_CONFIG "${CMAKE_SOURCE_DIR}/game/config/platform/nx/package.json")
set(ENGINE_NX_PACKAGE_SCHEMA "${CMAKE_SOURCE_DIR}/tools/schemas/package.schema.json")

foreach(_tool NX_ELF2NRO_EXE NX_NACPTOOL_EXE NX_UAM_EXE)
    if(NOT ${_tool})
        message(FATAL_ERROR
            "${_tool} was not found under $ENV{DEVKITPRO}/tools/bin.\n"
            "Install the toolchain with: dkp-pacman -S switch-dev\n"
            "See docs/nx/BUILD.md.")
    endif()
endforeach()

# One shader stage, compiled offline for the default renderer and embedded as text
# for the reference one, appending both headers to OUT_LIST.
function(nx_build_shader OUT_LIST STAGE SOURCE STEM GENERATED_DIR)
    set(_dksh "${GENERATED_DIR}/${STEM}_dksh.h")
    set(_glsl "${GENERATED_DIR}/${STEM}_glsl.h")
    string(REPLACE "scene_vert" "g_SceneVertex" _symbol "${STEM}")
    string(REPLACE "scene_frag" "g_SceneFragment" _symbol "${_symbol}")

    add_custom_command(
        OUTPUT "${_dksh}"
        COMMAND ${PYTHON3_BIN} "${ENGINE_NX_SHADER_TOOL}" dksh
                --uam "${NX_UAM_EXE}"
                --stage "${STAGE}"
                --input "${SOURCE}"
                --output "${_dksh}"
                --symbol "${_symbol}Dksh"
        DEPENDS "${SOURCE}" "${ENGINE_NX_SHADER_TOOL}"
        COMMENT "Compiling shader ${STEM} for deko3d"
        VERBATIM)

    add_custom_command(
        OUTPUT "${_glsl}"
        COMMAND ${PYTHON3_BIN} "${ENGINE_NX_SHADER_TOOL}" glsl
                --input "${SOURCE}"
                --output "${_glsl}"
                --symbol "${_symbol}Glsl"
        DEPENDS "${SOURCE}" "${ENGINE_NX_SHADER_TOOL}"
        COMMENT "Embedding shader ${STEM} for OpenGL"
        VERBATIM)

    set(${OUT_LIST} ${${OUT_LIST}} "${_dksh}" "${_glsl}" PARENT_SCOPE)
endfunction()

# Validate a package declaration against a title declaration and read it into
# ENGINE_NX_* variables in the caller's scope.
function(nx_read_package TITLE_PATH OUT_DIR)
    file(MAKE_DIRECTORY "${OUT_DIR}")
    set(_emitted "${OUT_DIR}/package.cmake")
    execute_process(
        COMMAND ${PYTHON3_BIN} "${ENGINE_NX_PACKAGE_TOOL}"
                --config "${ENGINE_NX_PACKAGE_CONFIG}"
                --schema "${ENGINE_NX_PACKAGE_SCHEMA}"
                --title "${TITLE_PATH}"
                --emit-cmake "${_emitted}"
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)

    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "nx package config rejected:\n${_err}${_out}\n"
            "Config: ${ENGINE_NX_PACKAGE_CONFIG}\n"
            "Title: ${TITLE_PATH}\n"
            "See docs/nx/PACKAGING.md.")
    endif()

    include("${_emitted}")
    foreach(_var ENGINE_NX_TITLE_NAME ENGINE_NX_TITLE_AUTHOR ENGINE_NX_TITLE_VERSION ENGINE_NX_ICON ENGINE_NX_PACKAGE_FILES)
        set(${_var} "${${_var}}" PARENT_SCOPE)
    endforeach()
endfunction()

# The container build, shared by the game and every example: stage the file
# system from an archive, generate the metadata and pack the executable. The
# commands are returned rather than attached, because the game owns a target of
# its own and an example extends its existing staging target.
function(nx_container_commands OUT_COMMANDS EXE_TARGET ARCHIVE_COMMANDS WORK_DIR NRO_PATH)
    set(_romfs "${WORK_DIR}/romfs")
    set(_nacp "${WORK_DIR}/control.nacp")
    get_filename_component(_nroDir "${NRO_PATH}" DIRECTORY)

    set(_commands
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_romfs}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_romfs}" "${_nroDir}"
        ${ARCHIVE_COMMANDS})

    foreach(_pair ${ENGINE_NX_PACKAGE_FILES})
        string(REPLACE "|" ";" _parts "${_pair}")
        list(GET _parts 0 _src)
        list(GET _parts 1 _dst)
        list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${_romfs}/${_dst}")
    endforeach()

    list(APPEND _commands
        COMMAND "${NX_NACPTOOL_EXE}" --create "${ENGINE_NX_TITLE_NAME}" "${ENGINE_NX_TITLE_AUTHOR}" "${ENGINE_NX_TITLE_VERSION}" "${_nacp}"
        COMMAND "${NX_ELF2NRO_EXE}" "$<TARGET_FILE:${EXE_TARGET}>" "${NRO_PATH}" "--icon=${ENGINE_NX_ICON}" "--nacp=${_nacp}" "--romfsdir=${_romfs}")

    set(${OUT_COMMANDS} ${_commands} PARENT_SCOPE)
endfunction()

function(platform_configure PLATFORM)
    engine_platform_dirs(${PLATFORM} _variantIncludeDir _baseIncludeDir _variantSrcDir _baseSrcDir)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_generated "${CMAKE_BINARY_DIR}/generated/${_lower}")
    file(MAKE_DIRECTORY "${_generated}")

    set(_shaderHeaders "")
    nx_build_shader(_shaderHeaders vert "${ENGINE_NX_CONFIG_DIR}/renderer/shaders/scene.vert.glsl" scene_vert "${_generated}")
    nx_build_shader(_shaderHeaders frag "${ENGINE_NX_CONFIG_DIR}/renderer/shaders/scene.frag.glsl" scene_frag "${_generated}")

    set(ENGINE_PLATFORM_${PLATFORM}_SOURCES
        "${_variantSrcDir}/Platform.cpp"
        "${_variantSrcDir}/Entry.cpp"
        "${_variantSrcDir}/Memory.cpp"
        "${_variantSrcDir}/Time.cpp"
        "${_variantSrcDir}/Thread.cpp"
        "${_variantSrcDir}/Console.cpp"
        "${_variantSrcDir}/Filesystem.cpp"
        "${_variantSrcDir}/Input.cpp"
        "${_variantSrcDir}/Applet.cpp"
        "${_variantSrcDir}/Window.cpp"
        "${_variantSrcDir}/Dialog.cpp"
        "${_variantIncludeDir}/Platform.h"
        "${_variantIncludeDir}/PlatformConstants.h"
        "${_variantSrcDir}/renderer/Deko3d.cpp"
        "${_variantIncludeDir}/renderer/Deko3d.h"
        "${_variantSrcDir}/renderer/OpenGl.cpp"
        "${_variantIncludeDir}/renderer/OpenGl.h"
        ${_shaderHeaders}
        PARENT_SCOPE)

    set(ENGINE_PLATFORM_${PLATFORM}_INCLUDES "${_generated}" PARENT_SCOPE)

    # Each library precedes what it depends on; libnx itself is appended last by
    # the vendor platform file.
    set(ENGINE_PLATFORM_${PLATFORM}_LIBS deko3d glad EGL glapi drm_nouveau PARENT_SCOPE)
endfunction()

function(platform_package PLATFORM EXE_TARGET DIST_DIR)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_work "${CMAKE_BINARY_DIR}/generated/${_lower}/package")
    nx_read_package("${CMAKE_SOURCE_DIR}/game/config/title.json" "${_work}")

    set(_cooked "${CMAKE_SOURCE_DIR}/dist/cooked/${_lower}")
    set(_archive
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/pack_master_archive.py"
                --rassets "${_cooked}/rassets"
                --prefix RASSETS
                --levels "${_cooked}/levels"
                --dst "${_work}/romfs/RASSETS.PS2R")

    set(_target "nro-${PLATFORM_${PLATFORM}_DIST}")
    nx_container_commands(_commands ${EXE_TARGET} "${_archive}" "${_work}" "${DIST_DIR}/game.nro")
    add_custom_target(${_target}
        ${_commands}
        COMMENT "Building dist/${PLATFORM_${PLATFORM}_DIST}/game.nro"
        VERBATIM
    )
    add_dependencies(${_target} ${EXE_TARGET} package-${_lower})

    set(PLATFORM_${PLATFORM}_PACKAGE_TARGET "${_target}" PARENT_SCOPE)
    set(PLATFORM_${PLATFORM}_CLEAN_PATHS "${CMAKE_BINARY_DIR}/generated/${_lower}" PARENT_SCOPE)
endfunction()

# An example is packaged exactly like the game, from its own title declaration
# and its own staged archive, as the last step of its staging target. Called by
# examples/CMakeLists.txt once that target exists.
function(platform_example_package PLATFORM NAME STAGE_TARGET EXE_TARGET DIST_DIR)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_work "${CMAKE_BINARY_DIR}/generated/${_lower}/examples/${NAME}")

    set(_title "${CMAKE_SOURCE_DIR}/examples/${NAME}/config/title.json")
    if(NOT EXISTS "${_title}")
        set(_title "${CMAKE_SOURCE_DIR}/game/config/title.json")
    endif()
    nx_read_package("${_title}" "${_work}")

    set(_archive
        COMMAND ${CMAKE_COMMAND} -E copy "${DIST_DIR}/RASSETS.PS2R" "${_work}/romfs/RASSETS.PS2R")

    nx_container_commands(_commands ${EXE_TARGET} "${_archive}" "${_work}" "${DIST_DIR}/${NAME}.nro")
    add_custom_command(TARGET ${STAGE_TARGET} POST_BUILD
        ${_commands}
        COMMENT "Building examples/dist/${NAME}/${_lower}/${NAME}.nro"
        VERBATIM
    )
endfunction()

# Launch the container in the emulator.
function(platform_run PLATFORM EXE_TARGET DIST_DIR)
    add_custom_target(run-${PLATFORM_${PLATFORM}_DIST}
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/run_target.py" --launcher ryujinx "${DIST_DIR}/game.nro"
        COMMENT "Launching dist/${PLATFORM_${PLATFORM}_DIST}/game.nro"
        VERBATIM
    )
endfunction()

# The container carries a stripped copy; the unstripped executable resolves addresses.
function(platform_debug_symbols PLATFORM EXE_TARGET DIST_DIR)
    if(NOT DEBUG)
        return()
    endif()

    add_custom_command(TARGET ${EXE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${EXE_TARGET}>" "${DIST_DIR}/main.debug.elf"
        COMMENT "Staging unstripped nx symbols: main.debug.elf"
        VERBATIM
    )
endfunction()
