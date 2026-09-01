# ---------------------------------------------------------------------------
# PlayStation Portable platform fragment. See docs/psp/BUILD.md.
#
# There is deliberately no platform_dependencies(): both renderers are served by
# libraries the toolchain already ships, so this platform vendors nothing.
# ---------------------------------------------------------------------------

set(PSP_PACKAGE_TOOL "${CMAKE_SOURCE_DIR}/tools/psp_package.py")
set(PSP_PACKAGE_CONFIG "${CMAKE_SOURCE_DIR}/game/config/platform/psp/package.json")
set(PSP_PACKAGE_SCHEMA "${CMAKE_SOURCE_DIR}/tools/schemas/package.schema.json")

find_program(PSP_MKSFOEX_BIN mksfoex PATHS "$ENV{PSPDEV}/bin" NO_DEFAULT_PATH)
find_program(PSP_PACK_PBP_BIN pack-pbp PATHS "$ENV{PSPDEV}/bin" NO_DEFAULT_PATH)
find_program(PSP_FIXUP_BIN psp-fixup-imports PATHS "$ENV{PSPDEV}/bin" NO_DEFAULT_PATH)
find_program(PSP_MKISOFS_BIN mkisofs)

# Validate the package declaration and read it into CMake variables.
function(psp_read_package PLATFORM OUT_GENERATED_DIR)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_generated "${CMAKE_BINARY_DIR}/generated/${_lower}")
    set(_emitted "${_generated}/package.cmake")

    file(MAKE_DIRECTORY "${_generated}")
    execute_process(
        COMMAND ${PYTHON3_BIN} "${PSP_PACKAGE_TOOL}"
                --config "${PSP_PACKAGE_CONFIG}"
                --schema "${PSP_PACKAGE_SCHEMA}"
                --generated-dir "${_generated}"
                --validate
                --emit-cmake "${_emitted}"
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)

    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "PSP package config rejected:\n${_err}${_out}\n"
            "Config: ${PSP_PACKAGE_CONFIG}\n"
            "See docs/psp/PACKAGING.md.")
    endif()

    include("${_emitted}")
    foreach(_var PSP_TITLE_ID PSP_TITLE_NAME PSP_TITLE_VERSION PSP_SFO_ARGS PSP_PBP_SLOTS PSP_PACKAGE_FILES)
        set(${_var} "${${_var}}" PARENT_SCOPE)
    endforeach()
    set(${OUT_GENERATED_DIR} "${_generated}" PARENT_SCOPE)
endfunction()

function(platform_configure PLATFORM)
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
        "${_variantSrcDir}/Dialog.cpp"
        "${_variantSrcDir}/UtilityDialog.cpp"
        "${_variantIncludeDir}/Platform.h"
        "${_variantIncludeDir}/PlatformConstants.h"
        "${_variantIncludeDir}/UtilityDialog.h"
        "${_variantIncludeDir}/Exit.h"
        "${_variantSrcDir}/renderer/Gu.cpp"
        "${_variantIncludeDir}/renderer/Gu.h"
        "${_variantSrcDir}/renderer/PspGl.cpp"
        "${_variantIncludeDir}/renderer/PspGl.h"
        PARENT_SCOPE)

    # pspgl needs the vector-unit context manager and the video-memory
    # allocator, and must precede both. The rest are the user-mode modules the
    # platform concerns use. All shipped by the toolchain; nothing is vendored.
    #
    # pspkernel is deliberately absent: it is the kernel-mode stub library and
    # redefines libc symbols, so linking it into a user-mode title fails on a
    # duplicate strtol rather than on anything that names the real cause. The
    # compiler's own specs already supply the user-mode defaults.
    set(ENGINE_PLATFORM_${PLATFORM}_LIBS
        GL GLU pspvfpu pspvram
        pspgu pspge pspdisplay pspctrl pspdmac psputility psprtc m
        PARENT_SCOPE)
endfunction()

# Two containers from one staged tree: the memory-card bundle and a disc image.
function(platform_package PLATFORM EXE_TARGET DIST_DIR)
    string(TOLOWER "${PLATFORM}" _lower)
    psp_read_package(${PLATFORM} _generated)

    set(_cooked "${CMAKE_SOURCE_DIR}/dist/cooked/${_lower}")
    set(_iso "${DIST_DIR}/game.iso")
    set(_isoRoot "${CMAKE_BINARY_DIR}/iso_root_${PLATFORM_${PLATFORM}_DIST}")

    if(NOT PSP_MKSFOEX_BIN OR NOT PSP_PACK_PBP_BIN)
        add_custom_target(pbp-${PLATFORM_${PLATFORM}_DIST}
            COMMENT "mksfoex/pack-pbp not found - skipping PSP packaging")
        return()
    endif()

    string(REPLACE ";" " " _sfoArgs "${PSP_SFO_ARGS}")
    separate_arguments(_sfoArgs UNIX_COMMAND "${_sfoArgs}")

    string(REPLACE ";" " " _pbpSlots "${PSP_PBP_SLOTS}")
    separate_arguments(_pbpSlots UNIX_COMMAND "${_pbpSlots}")

    set(_stageFiles "")
    foreach(_pair ${PSP_PACKAGE_FILES})
        string(REPLACE "|" ";" _parts "${_pair}")
        list(GET _parts 0 _src)
        list(GET _parts 1 _dst)
        list(APPEND _stageFiles COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${DIST_DIR}/${_dst}")
    endforeach()

    # The disc image needs the executable with its imports fixed up, which is
    # the same binary the memory-card container carries.
    set(_fixed "${_generated}/EBOOT.BIN")

    add_custom_target(pbp-${PLATFORM_${PLATFORM}_DIST}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/pack_master_archive.py"
                --rassets "${_cooked}/rassets"
                --prefix RASSETS
                --levels "${_cooked}/levels"
                --dst "${DIST_DIR}/RASSETS.PS2R"
        ${_stageFiles}

        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${EXE_TARGET}>" "${_fixed}"
        COMMAND ${PSP_FIXUP_BIN} "${_fixed}"
        COMMAND ${PSP_MKSFOEX_BIN} ${_sfoArgs} "${PSP_TITLE_NAME}" "${_generated}/PARAM.SFO"
        COMMAND ${PSP_PACK_PBP_BIN} "${DIST_DIR}/EBOOT.PBP" "${_generated}/PARAM.SFO" ${_pbpSlots} "${_fixed}" NULL
        COMMENT "Building dist/${PLATFORM_${PLATFORM}_DIST}/EBOOT.PBP"
        VERBATIM
    )
    add_dependencies(pbp-${PLATFORM_${PLATFORM}_DIST} ${EXE_TARGET} package-${_lower})

    if(PSP_MKISOFS_BIN)
        add_custom_target(iso-${PLATFORM_${PLATFORM}_DIST}
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${_isoRoot}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_isoRoot}/PSP_GAME/SYSDIR"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_isoRoot}/PSP_GAME/USRDIR"
            COMMAND ${CMAKE_COMMAND} -E copy "${_generated}/PARAM.SFO" "${_isoRoot}/PSP_GAME/PARAM.SFO"
            COMMAND ${CMAKE_COMMAND} -E copy "${_fixed}" "${_isoRoot}/PSP_GAME/SYSDIR/EBOOT.BIN"
            COMMAND ${CMAKE_COMMAND} -E copy "${_fixed}" "${_isoRoot}/PSP_GAME/SYSDIR/BOOT.BIN"
            COMMAND ${CMAKE_COMMAND} -E copy "${DIST_DIR}/RASSETS.PS2R" "${_isoRoot}/PSP_GAME/USRDIR/RASSETS.PS2R"
            COMMAND ${PYTHON3_BIN} "${PSP_PACKAGE_TOOL}"
                    --config "${PSP_PACKAGE_CONFIG}" --schema "${PSP_PACKAGE_SCHEMA}"
                    --generated-dir "${_generated}"
                    --emit-umd-data "${_isoRoot}/UMD_DATA.BIN"
            COMMAND ${PSP_MKISOFS_BIN} -quiet -iso-level 4 -V "${PSP_TITLE_ID}" -o "${_iso}" "${_isoRoot}"
            COMMENT "Building dist/${PLATFORM_${PLATFORM}_DIST}/game.iso"
            VERBATIM
        )
        add_dependencies(iso-${PLATFORM_${PLATFORM}_DIST} pbp-${PLATFORM_${PLATFORM}_DIST})
        add_dependencies(pbp-${PLATFORM_${PLATFORM}_DIST} ${EXE_TARGET})
        set(PLATFORM_${PLATFORM}_PACKAGE_TARGET "iso-${PLATFORM_${PLATFORM}_DIST}" PARENT_SCOPE)
    else()
        message(STATUS "  mkisofs not found - the PSP disc image will be skipped")
        set(PLATFORM_${PLATFORM}_PACKAGE_TARGET "pbp-${PLATFORM_${PLATFORM}_DIST}" PARENT_SCOPE)
    endif()

    set(PLATFORM_${PLATFORM}_CLEAN_PATHS "${_generated}" "${_isoRoot}" PARENT_SCOPE)
endfunction()

# Launch the memory-card container in the emulator. The launcher is named
# explicitly: a PSP disc image and a PlayStation 2 disc image share an extension.
function(platform_run PLATFORM EXE_TARGET DIST_DIR)
    add_custom_target(run-${PLATFORM_${PLATFORM}_DIST}
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/run_target.py" --launcher ppsspp "${DIST_DIR}/EBOOT.PBP"
        COMMENT "Launching dist/${PLATFORM_${PLATFORM}_DIST}/EBOOT.PBP"
        VERBATIM
    )
endfunction()

# The emulator resolves a symbol from an address out of the unstripped binary.
function(platform_debug_symbols PLATFORM EXE_TARGET DIST_DIR)
    if(NOT DEBUG)
        return()
    endif()

    add_custom_command(TARGET ${EXE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${EXE_TARGET}>" "${DIST_DIR}/main.debug.elf"
        COMMENT "Staging unstripped PSP symbols: main.debug.elf"
        VERBATIM
    )
endfunction()
