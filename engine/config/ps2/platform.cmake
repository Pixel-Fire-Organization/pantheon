# ---------------------------------------------------------------------------
# PlayStation 2 platform fragment.
#
# Included by cmake/Platforms.cmake ONLY when a PS2 platform survives selection,
# so a Win32-only build never touches ps2gl, ps2stuff, mkisofs or ee-nm.
#
# Sets, for each PS2 variant:
#   ENGINE_PLATFORM_<P>_SOURCES   platform base + variant translation units
#   ENGINE_PLATFORM_<P>_INCLUDES  extra include dirs (ps2gl)
#   ENGINE_PLATFORM_<P>_LIBS      link libraries
# and provides ps2_platform_package() to build the bootable ISO.
# ---------------------------------------------------------------------------

set(_PS2_DIR "${CMAKE_SOURCE_DIR}/engine/config/ps2")

# --- Disc identity ----------------------------------------------------------
# Lives here, not in the root: an ISO name and a disc serial mean nothing to a
# platform that does not boot from optical media.
set(APP_ISO_NAME "engine.iso")
set(APP_SERIAL "main.elf")

option(USERETAILNAME "Changes the executable name to the retail scheme." OFF)
if(USERETAILNAME)
    # OPL2 Manager (and OPL) parse the BOOT2 field of SYSTEM.CNF to extract a
    # serial; a plain "MAIN.ELF" name fails that parse.
    # Format: XXYY_NNN.NN (e.g. SLES_508.77 retail, SLHB_000.00 homebrew).
    set(APP_SERIAL "SLHB_000.00" CACHE STRING "PS2 game serial inside the ISO (XXYY_NNN.NN)")
endif()

# cdvdman performs case-sensitive UPPERCASE lookups in the ISO 9660 directory,
# and mkisofs -l preserves the case it is given, so a lowercase serial produces
# an entry cdvdman cannot find - "open fail" and a BIOS crash loop. Normalising
# here keeps SYSTEM.CNF and the directory entry in agreement.
string(TOUPPER "${APP_SERIAL}" APP_SERIAL)
string(REPLACE "." "" _APP_SERIAL_NODOT "${APP_SERIAL}")
string(REPLACE "_" "-" APP_SERIAL_LABEL "${_APP_SERIAL_NODOT}")
if(NOT USERETAILNAME)
    string(REPLACE ".iso" "" _APP_ISO_STEM "${APP_ISO_NAME}")
    string(TOUPPER "${_APP_ISO_STEM}" APP_SERIAL_LABEL)
endif()

set(ISO_CONTENT_BLACKLIST "maps;textures;models;engine") # cook/level sources; packed output lives in RASSETS.PS2R
find_program(MKISOFS_BIN mkisofs)

# --- Third-party dependencies -----------------------------------------------
# Called once per configure, before any engine target exists. ps2gl and ps2stuff
# are built here so a Win32-only build never touches the microcode toolchain.
function(platform_dependencies)
    add_subdirectory("${CMAKE_SOURCE_DIR}/external" "${CMAKE_BINARY_DIR}/external")

    # ps2gl is needed to LINK, not to compile: the engine library only needs its
    # headers. The driver applies this to each executable.
    foreach(_P ${ENGINE_ACTIVE_PLATFORMS})
        set(PLATFORM_${_P}_LINK_DEPS ps2gl_build PARENT_SCOPE)
        set(PLATFORM_${_P}_CLEAN_PATHS "${CMAKE_BINARY_DIR}/ps2gl_build" PARENT_SCOPE)
    endforeach()
endfunction()

function(platform_configure PLATFORM)
    engine_platform_dirs(${PLATFORM} _variantIncludeDir _baseIncludeDir _variantSrcDir _baseSrcDir)

    # A save directory the console can browse needs a descriptor and an icon
    # model. They carry the declared title, so they are generated from it rather
    # than committed, and compiled in so the engine can write them whatever
    # device it was launched from.
    set(PS2_ICON_GEN_DIR "${CMAKE_BINARY_DIR}/generated/ps2icon" CACHE INTERNAL "")
    set(_iconHeader "${PS2_ICON_GEN_DIR}/Ps2SaveIcon.h")
    if(NOT TARGET ps2-save-icon)
        add_custom_command(
            OUTPUT  "${_iconHeader}"
            COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/ps2/save_icon.py"
                    --declaration "${CMAKE_SOURCE_DIR}/game/config/title.json"
                    --emit-header "${_iconHeader}"
            DEPENDS "${CMAKE_SOURCE_DIR}/game/config/title.json" "${CMAKE_SOURCE_DIR}/tools/ps2/save_icon.py"
            COMMENT "Generating the PS2 save icon from title.json"
            VERBATIM
        )
        set_source_files_properties("${_iconHeader}" PROPERTIES GENERATED TRUE)
        add_custom_target(ps2-save-icon DEPENDS "${_iconHeader}")
    endif()

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
        "${_baseIncludeDir}/Platform.h"
        "${_baseIncludeDir}/PlatformConstantsPs2.h"
        "${_baseSrcDir}/renderer/Ps2Gl.cpp"
        "${_baseSrcDir}/renderer/GifTag.cpp"
        "${_baseIncludeDir}/renderer/Ps2Gl.h"
        "${_baseIncludeDir}/renderer/GifTag.h"
        "${_variantSrcDir}/Platform.cpp"
        "${_variantIncludeDir}/Platform.h"
        "${_variantIncludeDir}/PlatformConstants.h"
        PARENT_SCOPE)

    set(ENGINE_PLATFORM_${PLATFORM}_INCLUDES
        "${CMAKE_SOURCE_DIR}/external/ps2gl/include"
        "${PS2_ICON_GEN_DIR}"
        PARENT_SCOPE)

    # Link order matters: ps2stuff must follow ps2gl.
    set(ENGINE_PLATFORM_${PLATFORM}_LIBS
        ${PS2GL_LIB}
        ps2stuff dma packet2 graph draw input pad math3d gs png z m mc c -ldebug
        PARENT_SCOPE)

    # .clangd points editors at the PS2 toolchain headers; only meaningful when
    # that toolchain is present. One file per source tree, so generate it once
    # however many variants this configure builds.
    if(DEFINED ENV{PS2DEV} AND NOT _PS2_CLANGD_DONE)
        include("${_PS2_DIR}/ClangdConfig.cmake")
        set(_PS2_CLANGD_DONE TRUE CACHE INTERNAL "")
    endif()
endfunction()

# Per-variant packaging: SYSTEM.CNF with the matching VMODE, the staged disc
# tree, and a bootable ISO in that platform’s own dist/ bundle.
function(platform_package PLATFORM EXE_TARGET DIST_DIR)
    string(TOLOWER "${PLATFORM}" _lower)

    if(PLATFORM_${PLATFORM}_DIST STREQUAL "ps2pal")
        set(_vmode "PAL")
    else()
        set(_vmode "NTSC")
    endif()

    set(_serial "${APP_SERIAL}")
    set(_stage "${CMAKE_BINARY_DIR}/iso_root_${PLATFORM_${PLATFORM}_DIST}")
    set(_cnf "${CMAKE_BINARY_DIR}/SYSTEM.CNF.${PLATFORM_${PLATFORM}_DIST}")

    file(WRITE "${_cnf}" "BOOT2 = cdrom0:\\${_serial};1
VER = 1.00
VMODE = ${_vmode}
")

    if(NOT MKISOFS_BIN)
        add_custom_target(iso-${PLATFORM_${PLATFORM}_DIST}
            COMMENT "mkisofs not found - skipping ISO for ${PLATFORM}")
        return()
    endif()

    add_custom_target(iso-${PLATFORM_${PLATFORM}_DIST}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_stage}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_stage}"
        COMMAND ${CMAKE_COMMAND}
            -D "SRC=${CMAKE_SOURCE_DIR}/assets"
                -D "DST=${_stage}"
                -D "BLACKLIST=${ISO_CONTENT_BLACKLIST}"
                -P "${CMAKE_SOURCE_DIR}/tools/copy_iso_files.cmake"
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/pack_master_archive.py"
                --rassets "${CMAKE_SOURCE_DIR}/dist/cooked/${_lower}/rassets"
                --prefix RASSETS
                --levels "${CMAKE_SOURCE_DIR}/dist/cooked/${_lower}/levels"
                --dst "${_stage}/RASSETS.PS2R"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${EXE_TARGET}>" "${_stage}/${_serial}"
        COMMAND ${CMAKE_COMMAND} -E copy "${_cnf}" "${_stage}/SYSTEM.CNF"
        # -sysid "PLAYSTATION" is what disc tools check to accept the image as a
        # PS2 disc; the default ("LINUX") makes them report it invalid.
        COMMAND ${MKISOFS_BIN} -l -sysid "PLAYSTATION" -V "${APP_SERIAL_LABEL}"
                -o "${DIST_DIR}/${APP_ISO_NAME}" "${_stage}"
        COMMENT "Generating bootable ISO: dist/${PLATFORM_${PLATFORM}_DIST}/${APP_ISO_NAME}"
        VERBATIM
    )
    add_dependencies(iso-${PLATFORM_${PLATFORM}_DIST} ${EXE_TARGET} package-${_lower})
    set(PLATFORM_${PLATFORM}_PACKAGE_TARGET "iso-${PLATFORM_${PLATFORM}_DIST}" PARENT_SCOPE)

endfunction()

# PCSX2 reads a .sym next to the ELF for source-level debugging.
function(platform_debug_symbols PLATFORM EXE_TARGET DIST_DIR)
    if(NOT DEBUG)
        return()
    endif()

    get_filename_component(_toolchainBin "${CMAKE_C_COMPILER}" DIRECTORY)
    find_program(EE_NM_BIN NAMES mips64r5900el-ps2-elf-nm ee-nm PATHS "${_toolchainBin}" NO_DEFAULT_PATH)
    if(NOT EE_NM_BIN)
        message(WARNING "ee-nm not found - PCSX2 .sym generation skipped for ${PLATFORM}.")
        return()
    endif()

    get_filename_component(_stem "${PLATFORM_${PLATFORM}_EXE}" NAME_WE)
    add_custom_command(TARGET ${EXE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D "EE_NM=${EE_NM_BIN}"
            -D "ELF=$<TARGET_FILE:${EXE_TARGET}>"
            -D "SYM=${DIST_DIR}/${_stem}.sym"
            -P "${CMAKE_SOURCE_DIR}/tools/gen_sym.cmake"
        COMMENT "Generating PCSX2 debug symbols: ${_stem}.sym"
        VERBATIM
    )

endfunction()

# Launch the built disc image in an emulator.
function(platform_run PLATFORM EXE_TARGET DIST_DIR)
    add_custom_target(run-${PLATFORM_${PLATFORM}_DIST}
        COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/run_target.py" "${DIST_DIR}/${APP_ISO_NAME}"
        COMMENT "Launching dist/${PLATFORM_${PLATFORM}_DIST}/${APP_ISO_NAME}"
        VERBATIM
    )
endfunction()
