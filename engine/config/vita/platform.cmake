# PlayStation Vita platform fragment. See docs/vita/BUILD.md.

set(_VITA_DIR "${CMAKE_SOURCE_DIR}/engine/config/vita")

set(VITAGL_DIR "${CMAKE_SOURCE_DIR}/external/vitaGL")
set(VITAGL_LIB "${VITAGL_DIR}/libvitaGL.a")

# Build vitaGL before any engine target exists.
function(platform_dependencies)
    if(NOT EXISTS "${VITAGL_DIR}/Makefile")
        message(FATAL_ERROR
            "external/vitaGL is empty. Run: git submodule update --init --recursive")
    endif()

    add_custom_command(
        OUTPUT "${VITAGL_LIB}"
        COMMAND ${CMAKE_COMMAND} -E env "VITASDK=$ENV{VITASDK}" "PATH=$ENV{VITASDK}/bin:$ENV{PATH}"
                ${CMAKE_MAKE_PROGRAM} NO_DEBUG=1 NO_SPLASHSCREEN=1
        WORKING_DIRECTORY "${VITAGL_DIR}"
        COMMENT "Building vitaGL..."
        VERBATIM)
    add_custom_target(vitagl_build DEPENDS "${VITAGL_LIB}")

    foreach(_P ${ENGINE_ACTIVE_PLATFORMS})
        set(PLATFORM_${_P}_LINK_DEPS vitagl_build PARENT_SCOPE)
    endforeach()
endfunction()

if(DEFINED ENV{PSP2CGC} AND EXISTS "$ENV{PSP2CGC}")
    set(PSP2CGC_BIN "$ENV{PSP2CGC}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/external/psp2cgc/psp2cgc.exe")
    set(PSP2CGC_BIN "${CMAKE_SOURCE_DIR}/external/psp2cgc/psp2cgc.exe")
else()
    message(FATAL_ERROR
        "psp2cgc not found - the Vita graphics backends cannot compile their shaders.\n"
        "Looked for:\n"
        "  $PSP2CGC (currently '$ENV{PSP2CGC}')\n"
        "  ${CMAKE_SOURCE_DIR}/external/psp2cgc/psp2cgc.exe\n"
        "\n"
        "It is not committed to this repository on purpose. Fetch it into\n"
        "external/psp2cgc/ or point PSP2CGC at your own copy.\n"
        "See docs/vita/BUILD.md.")
endif()
message(STATUS "  psp2cgc: ${PSP2CGC_BIN}")

set(VITA_SHADER_TOOL "${CMAKE_SOURCE_DIR}/tools/compile_shader.py")

# Compile one shader to a linkable header, appending it to OUT_LIST.
function(vita_compile_shader OUT_LIST PROFILE SOURCE SYMBOL GENERATED_DIR)
    get_filename_component(_stem "${SOURCE}" NAME_WE)
    set(_header "${GENERATED_DIR}/${_stem}.h")

    set(_require "")
    foreach(_param ${ARGN})
        list(APPEND _require "--require" "${_param}")
    endforeach()

    add_custom_command(
        OUTPUT "${_header}"
        COMMAND ${PYTHON3_BIN} "${VITA_SHADER_TOOL}"
                --compiler "${PSP2CGC_BIN}"
                --profile "${PROFILE}"
                --input "${SOURCE}"
                --output "${_header}"
                --symbol "${SYMBOL}"
                ${_require}
        DEPENDS "${SOURCE}" "${VITA_SHADER_TOOL}"
        COMMENT "Compiling shader ${_stem}.cg (${PROFILE})"
        VERBATIM)

    set(${OUT_LIST} ${${OUT_LIST}} "${_header}" PARENT_SCOPE)
endfunction()

set(VITA_PACKAGE_TOOL   "${CMAKE_SOURCE_DIR}/tools/vita_package.py")
set(VITA_PACKAGE_CONFIG "${CMAKE_SOURCE_DIR}/game/config/platform/vita/package.json")
set(VITA_PACKAGE_SCHEMA "${CMAKE_SOURCE_DIR}/tools/schemas/package.schema.json")

# Validate the package config and read it into CMake variables.
function(vita_read_package PLATFORM OUT_GENERATED_DIR)
    string(TOLOWER "${PLATFORM}" _variant)
    set(_generated "${CMAKE_BINARY_DIR}/generated/${_variant}")
    set(_emitted "${_generated}/package.cmake")

    file(MAKE_DIRECTORY "${_generated}")
    execute_process(
        COMMAND ${PYTHON3_BIN} "${VITA_PACKAGE_TOOL}"
                --config "${VITA_PACKAGE_CONFIG}"
                --schema "${VITA_PACKAGE_SCHEMA}"
                --variant "${_variant}"
                --generated-dir "${_generated}"
                --validate
                --emit-cmake "${_emitted}"
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)

    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "Vita package config rejected for ${PLATFORM}:\n${_err}${_out}\n"
            "Config: ${VITA_PACKAGE_CONFIG}\n"
            "See docs/vita/PACKAGING.md.")
    endif()

    include("${_emitted}")
    foreach(_var VITA_TITLE_ID VITA_TITLE_NAME VITA_TITLE_VERSION VITA_MKSFOEX_ARGS
                 VITA_MAKE_FSELF_ARGS VITA_PACKAGE_FILES VITA_TROPHIES_ENABLED
                 VITA_TROPHY_GENERATE VITA_NP_COMM_ID VITA_TROPHY_COUNT)
        set(${_var} "${${_var}}" PARENT_SCOPE)
    endforeach()
    set(${OUT_GENERATED_DIR} "${_generated}" PARENT_SCOPE)
endfunction()

set(VITA_TRPWORK "${CMAKE_SOURCE_DIR}/external/trpwork/Win32/Release/TRPWork.exe" CACHE FILEPATH
    "TRPWork, which packs the trophy container. See docs/formats/TROPHY_PACK.md.")

set(VITA_TRP_MAGIC "" CACHE STRING
    "Override the generated TROPHY.TRP container magic. Empty uses the established value; see docs/formats/TROPHY_PACK.md.")

function(platform_configure PLATFORM)
    engine_platform_dirs(${PLATFORM} _variantIncludeDir _baseIncludeDir _variantSrcDir _baseSrcDir)
    vita_read_package(${PLATFORM} _generated)

    set(_shaderHeaders "")
    vita_compile_shader(_shaderHeaders vertex   "${_VITA_DIR}/renderer/shaders/scene_v.cg" g_SceneVertexGxp   "${_generated}"
                        aPosition aTexcoord aColor uViewProj)
    vita_compile_shader(_shaderHeaders fragment "${_VITA_DIR}/renderer/shaders/scene_f.cg" g_SceneFragmentGxp "${_generated}")

    set(ENGINE_PLATFORM_${PLATFORM}_SOURCES
        "${_baseSrcDir}/Platform.cpp"
        "${_baseSrcDir}/Entry.cpp"
        "${_baseSrcDir}/Memory.cpp"
        "${_baseSrcDir}/Time.cpp"
        "${_baseSrcDir}/Thread.cpp"
        "${_baseSrcDir}/Console.cpp"
        "${_baseSrcDir}/Filesystem.cpp"
        "${_baseSrcDir}/Input.cpp"
        "${_baseSrcDir}/Window.cpp"
        "${_baseSrcDir}/CommonDialog.cpp"
        "${_baseSrcDir}/Dialog.cpp"
        "${_baseIncludeDir}/CommonDialog.h"
        "${_baseIncludeDir}/Platform.h"
        "${_baseIncludeDir}/PlatformConstantsVita.h"
        "${_baseSrcDir}/renderer/Gxm.cpp"
        "${_baseIncludeDir}/renderer/Gxm.h"
        "${_baseSrcDir}/renderer/VitaGl.cpp"
        "${_baseIncludeDir}/renderer/VitaGl.h"
        ${_shaderHeaders}
        "${_variantSrcDir}/Platform.cpp"
        "${_variantIncludeDir}/Platform.h"
        "${_variantIncludeDir}/PlatformConstants.h"
        PARENT_SCOPE)

    set(ENGINE_PLATFORM_${PLATFORM}_INCLUDES "${_generated}" "${VITAGL_DIR}/source" PARENT_SCOPE)

    set(ENGINE_PLATFORM_${PLATFORM}_DEFINES
        "VITA_TITLE_ID_STR=\"${VITA_TITLE_ID}\""
        PARENT_SCOPE)

    # Not optional: see docs/vita/BUILD.md. The executable conversion appends
    # module metadata after the code segment and can only use the padding the
    # linker left before the next one; without this the two segments are packed
    # tight enough that the conversion fails rather than the link. max-page-size
    # raises the ceiling on that gap -- one of two changes BUILD.md documents
    # for this failure; the other was removing the SCE data the trophy
    # integration's imports no longer contribute now that it is gone.
    set(ENGINE_PLATFORM_${PLATFORM}_LIBS
        "-Wl,-z,separate-code"
        "-Wl,-z,max-page-size=0x10000"
        "${VITAGL_LIB}"
        vitashark SceShaccCgExt taihen_stub SceShaccCg_stub mathneon
        SceDisplay_stub SceGxm_stub
        SceCtrl_stub SceTouch_stub
        SceLibKernel_stub SceSysmem_stub SceKernelThreadMgr_stub
        SceIofilemgr_stub SceSysmodule_stub SceKernelDmacMgr_stub
        SceAppMgr_stub SceProcessmgr_stub ScePower_stub SceCommonDialog_stub
        SceIme_stub
        m
        PARENT_SCOPE)
endfunction()

# Build the installable .vpk for one variant.
function(platform_package PLATFORM EXE_TARGET DIST_DIR)
    string(TOLOWER "${PLATFORM}" _variant)
    vita_read_package(${PLATFORM} _generated)

    set(_velf   "${_generated}/${_variant}.velf")
    set(_eboot  "${_generated}/eboot.bin")
    set(_sfo    "${_generated}/param.sfo")
    set(_vpk    "${DIST_DIR}/${VITA_TITLE_ID}.vpk")

    string(REPLACE ";" " " _mksfoexArgs "${VITA_MKSFOEX_ARGS}")
    separate_arguments(_mksfoexArgs UNIX_COMMAND "${_mksfoexArgs}")

    string(REPLACE ";" " " _fselfArgs "${VITA_MAKE_FSELF_ARGS}")
    separate_arguments(_fselfArgs UNIX_COMMAND "${_fselfArgs}")

    # The container is built by TRPWork, not here: it owns the payload
    # alignment and the digest, and it is the reference reader for this format.
    # We supply the entry table and the payloads; it packs them.
    set(_trophyGen "")
    if(VITA_TROPHY_GENERATE)
        if(NOT EXISTS "${VITA_TRPWORK}")
            message(FATAL_ERROR
                "trophies.enabled is set for ${PLATFORM}, but TRPWork is missing at ${VITA_TRPWORK}.
"
                "Run: git submodule update --init external/trpwork")
        endif()
        set(_trophyGen
            --emit-trophy-conf "${_generated}"
            --emit-trp-index "${_generated}"
            --trpwork "${VITA_TRPWORK}")
    endif()

    set(_regen
        COMMAND ${PYTHON3_BIN} "${VITA_PACKAGE_TOOL}"
                --config "${VITA_PACKAGE_CONFIG}" --schema "${VITA_PACKAGE_SCHEMA}"
                --variant "${_variant}" --generated-dir "${_generated}"
                --validate
                --emit-template "${_generated}/template.xml"
                --emit-ids "${_generated}/TrophyIds.h"
                ${_trophyGen})

    set(_addArgs "")
    foreach(_pair ${VITA_PACKAGE_FILES})
        string(REPLACE "|" ";" _parts "${_pair}")
        list(GET _parts 0 _src)
        list(GET _parts 1 _dst)
        list(APPEND _addArgs "-a" "${_src}=${_dst}")
    endforeach()
    list(APPEND _addArgs
        "-a" "${DIST_DIR}/RASSETS.PS2R=RASSETS.PS2R")

    add_custom_target(vpk-${PLATFORM_${PLATFORM}_DIST}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
        ${_regen}
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/pack_master_archive.py"
                --rassets "${CMAKE_SOURCE_DIR}/dist/cooked/${_variant}/rassets"
                --prefix RASSETS
                --levels "${CMAKE_SOURCE_DIR}/dist/cooked/${_variant}/levels"
                --dst "${DIST_DIR}/RASSETS.PS2R"
        # EXE_TARGET's link crosses the WSL/DrvFs boundary this project builds
        # on; make's dependency ordering guarantees that process exited but
        # not that the write is visible yet through that bridge. Left in
        # place as a harmless precaution, not because it was shown to matter
        # -- see docs/vita/BUILD.md for what actually explained the
        # segment-overlap failure this was investigated alongside.
        COMMAND sync
        COMMAND ${VITA_ELF_CREATE} "$<TARGET_FILE:${EXE_TARGET}>" "${_velf}"
        COMMAND ${VITA_MAKE_FSELF} ${_fselfArgs} "${_velf}" "${_eboot}"
        COMMAND ${VITA_MKSFOEX} ${_mksfoexArgs} "${VITA_TITLE_NAME}" "${_sfo}"
        COMMAND ${VITA_PACK_VPK} -s "${_sfo}" -b "${_eboot}" ${_addArgs} "${_vpk}"
        COMMENT "Building dist/${PLATFORM_${PLATFORM}_DIST}/${VITA_TITLE_ID}.vpk"
        VERBATIM
    )
    add_dependencies(vpk-${PLATFORM_${PLATFORM}_DIST} ${EXE_TARGET} package-${_variant})
    set(PLATFORM_${PLATFORM}_PACKAGE_TARGET "vpk-${PLATFORM_${PLATFORM}_DIST}" PARENT_SCOPE)
    set(PLATFORM_${PLATFORM}_CLEAN_PATHS "${_generated}" PARENT_SCOPE)
endfunction()

# Launch the built package in an emulator.
function(platform_run PLATFORM EXE_TARGET DIST_DIR)
    vita_read_package(${PLATFORM} _generated)

    add_custom_target(run-${PLATFORM_${PLATFORM}_DIST}
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/run_target.py" "${DIST_DIR}/${VITA_TITLE_ID}.vpk"
        COMMENT "Launching dist/${PLATFORM_${PLATFORM}_DIST}/${VITA_TITLE_ID}.vpk"
        VERBATIM
    )
endfunction()
