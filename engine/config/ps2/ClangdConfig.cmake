# ---------------------------------------------------------------------------
# Generate a .clangd so editors resolve the PS2 toolchain headers. Git-ignored:
# each developer produces their own by configuring. Never edit .clangd by hand.
# ---------------------------------------------------------------------------

if(DEFINED ENV{PS2SDK})
    set(_CLANGD_SDK_PATH "$ENV{PS2SDK}")
else()
    set(_CLANGD_SDK_PATH "$ENV{PS2DEV}/ps2sdk")
endif()

set(_CLANGD_ENG_PATH "${CMAKE_SOURCE_DIR}")

# Translate WSL paths to Windows UNC form so the editor on the host resolves them.
find_program(WSLPATH_BIN wslpath)
if(WSLPATH_BIN)
    execute_process(COMMAND ${WSLPATH_BIN} -w "${CMAKE_SOURCE_DIR}"
                    OUTPUT_VARIABLE _CLANGD_ENG_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${WSLPATH_BIN} -w "${_CLANGD_SDK_PATH}"
                    OUTPUT_VARIABLE _CLANGD_SDK_PATH_WIN OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "\\" "/" _CLANGD_ENG_PATH "${_CLANGD_ENG_PATH}")
    string(REPLACE "\\" "/" _CLANGD_SDK_PATH "${_CLANGD_SDK_PATH_WIN}")
endif()

# Point the editor at the default platform, so PlatformConstants.h resolves.
set(_CLANGD_PLATFORM_DIR "${_CLANGD_ENG_PATH}/engine/include/platform/${PLATFORM_${ENGINE_DEFAULT_PLATFORM}_DIR}")
set(_CLANGD_PLATFORM_BASE "${_CLANGD_ENG_PATH}/engine/include/platform/${PLATFORM_${ENGINE_DEFAULT_PLATFORM}_BASE}")

file(WRITE "${CMAKE_SOURCE_DIR}/.clangd"
    "CompileFlags:
"
    "  Add:
"
    "    - \"-I${_CLANGD_ENG_PATH}/engine/include\"
"
    "    - \"-I${_CLANGD_PLATFORM_DIR}\"
"
    "    - \"-I${_CLANGD_PLATFORM_BASE}\"
"
    "    - \"-I${_CLANGD_ENG_PATH}/external/ps2gl/include\"
"
    "    - \"-I${_CLANGD_SDK_PATH}/ee/include\"
"
    "    - \"-I${_CLANGD_SDK_PATH}/common/include\"
"
    "    - \"-I${_CLANGD_SDK_PATH}/ports/include\"
"
    "    - \"-D_PS2\"
"
    "    - \"-D_EE\"
"
    "
"
    "  CompilationDatabase: build
"
)
message(STATUS "Generated .clangd for ${ENGINE_DEFAULT_PLATFORM}")
