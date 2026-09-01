# ---------------------------------------------------------------------------
# The engine library, built once per active platform.
#
# Each platform gets its own library rather than one shared build: platform
# constants are compile-time (PlatformConstants.h resolves through the include
# path), and the PAL/NTSC screen dimensions in particular are multiplied per
# transformed vertex in the GIFTAG renderer, where a runtime lookup would cost
# the EE dearly.
# ---------------------------------------------------------------------------

# Defined here, ahead of ENGINE_CORE_SOURCES below, because that list already
# references it (the generated theme table). A CACHE variable is visible from
# the start of every configure after the first, which is why this ordering
# only matters -- and only fails silently into an empty path -- on a build
# directory's very first configure.
set(THEME_GEN_DIR "${CMAKE_BINARY_DIR}/generated/theme" CACHE INTERNAL "")
set(LOADING_SCREEN_GEN_DIR "${CMAKE_BINARY_DIR}/generated/loading_screen" CACHE INTERNAL "")

set(ENGINE_CORE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/CommandLine.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/GameAPI.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/EngineMain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/EngineCore.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/EngineDebug.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/EngineNotice.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/EngineAchievementUi.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/EngineMemory.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/EngineSubsystems.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformRegistry.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformContract.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/EngineIO.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/EngineArchive.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/EngineResource.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/level/EngineLevel.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/level/EngineSector.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/scenes/EngineScene.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/scenes/LevelScene.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/scenes/UIScene.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/scenes/EngineLoadingScreen.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/EngineApp.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/EngineInput.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/EngineAchievement.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/EngineAction.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Ui.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/UiWidgets.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/UiFont.cpp"
        "${THEME_GEN_DIR}/UiThemeTable.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/UiLayout.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/UiOverlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/UiState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/UiTheme.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/UiFontAsset.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/UiStyle.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/UiIcon.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/UiTable.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/UiMenu.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/UiDialog.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/NullRenderer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/tim2.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/FontLoad.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/ModelLoad.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/Transform3D.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/DrawList.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/StagedGeometry.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/TextureExpand.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/Frustum.cpp"
        CACHE INTERNAL "")

set(ENGINE_CORE_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/core/CommandLine.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/core/EngineMain.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/Platform.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/MemoryContract.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/PlatformKeys.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/PlatformRegistry.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/PlatformTypes.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/Engine.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/core/EngineApp.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/GameAPI.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/Macros.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/core/EngineCore.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/core/EngineDebug.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/core/EngineNotice.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/ui/EngineAchievementUi.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/core/EngineIO.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/resources/EngineArchive.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/core/EngineMemory.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/core/EngineSubsystems.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/resources/EngineResource.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/level/EngineLevel.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/level/EngineLevelFormat.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/level/EngineSector.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/scenes/EngineScene.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/scenes/EngineLoadingScreen.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/core/EngineInput.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/core/EngineAchievement.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/core/EngineAction.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/platform/AchievementContract.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/graphics/EngineGraphics.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/graphics/NullRenderer.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/graphics/Renderer.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/graphics/Primitives.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/graphics/Types.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/graphics/UI.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/ui/EngineUi.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/graphics/DrawList.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/graphics/StagedGeometry.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/graphics/TextureExpand.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/graphics/Frustum.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/graphics/ModelFormat.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/scenes/Scene.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/scenes/LevelScene.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/scenes/UIScene.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/scenes/FreeFlowScene.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/scenes/SceneResource.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/ecs/Entity.h"
        CACHE INTERNAL "")

set(ENGINE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include" CACHE INTERNAL "")

# The debug testbed is present only in a debug configuration: not switched off
# at run time, absent. Which sources are compiled expresses that, the same way
# it expresses platform choice. See docs/subsystems/TESTBED.md.
if (DEBUG)
    set(ENGINE_DEBUG_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/Testbed.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/TestbedScenes.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/ScenePlatformInfo.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/SceneGamepad.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/SceneKeyboardMouse.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/SceneTouch.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/ScenePointer.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/SceneChords.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/SceneScreenAspect.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/SceneDepthRange.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/SceneMemory.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/SceneResources.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/SceneAssetBrowser.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/SceneLevelStream.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/SceneAchievements.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/ScenePerformance.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/scenes/SceneFramePacing.cpp"
            CACHE INTERNAL "")
else ()
    set(ENGINE_DEBUG_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/Stub.cpp"
            CACHE INTERNAL "")
endif ()


find_program(PYTHON3_BIN python3 REQUIRED)

# --- Title identity ---------------------------------------------------------
# game/config/title.json is the single source of who made the title and what
# each platform files it under. A platform builds its writable-storage location
# from this, so the identity a console shows and the identity a save is filed
# under cannot disagree.
set(ENGINE_ASSETS_SRC_DIR "${CMAKE_SOURCE_DIR}/assets/engine" CACHE INTERNAL "Engine-owned source assets")

set(TITLE_GEN_DIR "${CMAKE_BINARY_DIR}/generated/title" CACHE INTERNAL "")
set(TITLE_JSON "${CMAKE_SOURCE_DIR}/game/config/title.json")
set(TITLE_GEN_PY "${CMAKE_SOURCE_DIR}/tools/title.py")

add_custom_command(
        OUTPUT "${TITLE_GEN_DIR}/TitleInfo.h"
        COMMAND ${PYTHON3_BIN} "${TITLE_GEN_PY}"
        --declaration "${TITLE_JSON}"
        --emit-header "${TITLE_GEN_DIR}/TitleInfo.h"
        DEPENDS "${TITLE_JSON}" "${TITLE_GEN_PY}"
        COMMENT "Generating TitleInfo.h from title.json"
        VERBATIM
)
set_source_files_properties("${TITLE_GEN_DIR}/TitleInfo.h" PROPERTIES GENERATED TRUE)
add_custom_target(title-generate DEPENDS "${TITLE_GEN_DIR}/TitleInfo.h")

# game/config/theme.json is the single source of truth for the interface's
# look. Its themes are compiled in, because the interface must be able to draw
# before a filesystem exists and because a built-in theme is what a refused
# load falls back to. The generator checks the declaration against UiColor, so
# a role added to the engine and forgotten there fails the build rather than
# producing a theme with a missing colour.
set(THEME_JSON "${CMAKE_SOURCE_DIR}/game/config/theme.json")
set(THEME_GEN_PY "${CMAKE_SOURCE_DIR}/tools/theme.py")

add_custom_command(
        OUTPUT "${THEME_GEN_DIR}/UiThemeIds.h"
        "${THEME_GEN_DIR}/UiThemeTable.cpp"
        COMMAND ${PYTHON3_BIN} "${THEME_GEN_PY}"
        --declaration "${THEME_JSON}"
        --emit-ids "${THEME_GEN_DIR}/UiThemeIds.h"
        --emit-table "${THEME_GEN_DIR}/UiThemeTable.cpp"
        DEPENDS "${THEME_JSON}" "${THEME_GEN_PY}" "include/ui/EngineUi.h"
        COMMENT "Generating themes (UiThemeIds.h, UiThemeTable.cpp) from theme.json"
        VERBATIM
)
set_source_files_properties(
        "${THEME_GEN_DIR}/UiThemeIds.h"
        "${THEME_GEN_DIR}/UiThemeTable.cpp"
        PROPERTIES GENERATED TRUE
)
add_custom_target(theme-generate DEPENDS "${THEME_GEN_DIR}/UiThemeIds.h" "${THEME_GEN_DIR}/UiThemeTable.cpp")

# game/config/loading_screen.json declares the images the built-in loading
# screen cycles through while a scene's declared resources are outstanding.
# They must reach C++ running on a JSON-less console, so — like theme.json —
# the declaration is compiled in rather than parsed at runtime.
set(LOADING_SCREEN_JSON "${CMAKE_SOURCE_DIR}/game/config/loading_screen.json")
set(LOADING_SCREEN_GEN_PY "${CMAKE_SOURCE_DIR}/tools/loading_screen.py")

add_custom_command(
        OUTPUT "${LOADING_SCREEN_GEN_DIR}/LoadingScreenAssets.h"
        COMMAND ${PYTHON3_BIN} "${LOADING_SCREEN_GEN_PY}"
        --declaration "${LOADING_SCREEN_JSON}"
        --emit-header "${LOADING_SCREEN_GEN_DIR}/LoadingScreenAssets.h"
        DEPENDS "${LOADING_SCREEN_JSON}" "${LOADING_SCREEN_GEN_PY}"
        COMMENT "Generating LoadingScreenAssets.h from loading_screen.json"
        VERBATIM
)
set_source_files_properties("${LOADING_SCREEN_GEN_DIR}/LoadingScreenAssets.h" PROPERTIES GENERATED TRUE)
add_custom_target(loading-screen-generate DEPENDS "${LOADING_SCREEN_GEN_DIR}/LoadingScreenAssets.h")


# Build the engine library for one platform. That platform.cmake fragment has
# already run and set ENGINE_PLATFORM_<P>_SOURCES / _LIBS / _DEFINES / _INCLUDES.
function(engine_add_library PLATFORM OUT_TARGET)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_target "engine_${_lower}")

    engine_platform_dirs(${PLATFORM} _variantIncludeDir _baseIncludeDir _variantSrcDir _baseSrcDir)

    # This function is called from the root list file, so the GENERATED property
    # set at this file's scope does not reach the target; mark it again here.
    set_source_files_properties("${THEME_GEN_DIR}/UiThemeTable.cpp" PROPERTIES GENERATED TRUE)

    add_library(${_target} STATIC
            ${ENGINE_CORE_SOURCES}
            ${ENGINE_CORE_HEADERS}
            ${ENGINE_DEBUG_SOURCES}
            ${ENGINE_PLATFORM_${PLATFORM}_SOURCES}
    )

    # "PlatformConstants.h" resolves here: the variant directory first, then the
    # shared base. This is what lets platform constants stay compile-time without
    # any shared engine file naming a platform.
    target_include_directories(${_target} PUBLIC
            "${ENGINE_INCLUDE_DIR}"
            "${TITLE_GEN_DIR}"
            "${THEME_GEN_DIR}"
            "${LOADING_SCREEN_GEN_DIR}"
            "${_variantIncludeDir}"
    )
    add_dependencies(${_target} title-generate theme-generate loading-screen-generate)
    if (TARGET ps2-save-icon)
        add_dependencies(${_target} ps2-save-icon)
    endif ()
    if (_baseIncludeDir)
        target_include_directories(${_target} PUBLIC "${_baseIncludeDir}")
    endif ()
    if (ENGINE_PLATFORM_${PLATFORM}_INCLUDES)
        target_include_directories(${_target} PUBLIC ${ENGINE_PLATFORM_${PLATFORM}_INCLUDES})
    endif ()

    if (ENGINE_PLATFORM_${PLATFORM}_DEFINES)
        target_compile_definitions(${_target} PUBLIC ${ENGINE_PLATFORM_${PLATFORM}_DEFINES})
    endif ()
    if (ENGINE_PLATFORM_${PLATFORM}_LIBS)
        target_link_libraries(${_target} PUBLIC ${ENGINE_PLATFORM_${PLATFORM}_LIBS})
    endif ()

    target_compile_options(${_target} PRIVATE -Wall -Wextra -Werror)
    set_target_properties(${_target} PROPERTIES LINKER_LANGUAGE CXX)

    set(${OUT_TARGET} "${_target}" PARENT_SCOPE)
endfunction()
