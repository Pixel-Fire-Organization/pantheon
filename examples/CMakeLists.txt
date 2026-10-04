# ---------------------------------------------------------------------------
# Standalone examples.
#
# Each example is its own executable per active platform, isolated from every
# other example and from the game: its own process, its own memory, its own
# runtime-reset boundary. See docs/EXAMPLES.md and
# docs/guidelines/NEW_EXAMPLE.md.
#
# Registering a new example is adding its name to EXAMPLE_NAMES below plus its
# own EXAMPLE_<name>_SOURCES / EXAMPLE_<name>_INCLUDE_DIR — the per-platform
# loop in the root CMakeLists.txt instantiates it for every active platform
# automatically, the same shape game_add_executable() already has for game/.
# ---------------------------------------------------------------------------

set(EXAMPLE_NAMES primitives ui_gallery material_pbr CACHE INTERNAL "")

set(EXAMPLE_primitives_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/primitives/src/Game.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/primitives/src/scenes/ColouredPrimitivesScene.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/primitives/src/scenes/TexturedPrimitivesScene.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/primitives/src/scenes/DrawLoadScene.cpp"
        CACHE INTERNAL "")
set(EXAMPLE_primitives_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/primitives/include" CACHE INTERNAL "")

set(EXAMPLE_ui_gallery_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/ui_gallery/src/Game.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui_gallery/src/scenes/GalleryScene.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui_gallery/src/scenes/StyleScene.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui_gallery/src/scenes/BudgetScene.cpp"
        CACHE INTERNAL "")
set(EXAMPLE_ui_gallery_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ui_gallery/include" CACHE INTERNAL "")

set(EXAMPLE_material_pbr_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/material_pbr/src/Game.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/material_pbr/src/EcsHooks.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/material_pbr/src/scenes/MaterialLightingScene.cpp"
        CACHE INTERNAL "")
set(EXAMPLE_material_pbr_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/material_pbr/include" CACHE INTERNAL "")

# Which assets/maps/*.map this example actually loads (see
# EngineSubsystem::Level/Sector in its Game.cpp and MaterialLightingScene's
# GetLevelName()) -- not every map under assets/maps/, mirroring the scoping
# example_add_cook_target already gives per-example cooked assets. Declaring
# this is what opts an example into level compilation at all: see
# example_add_level_compile_target() below and its guarded call in the root
# CMakeLists.txt's per-platform loop.
set(EXAMPLE_material_pbr_LEVEL_MAPS
        "${CMAKE_SOURCE_DIR}/assets/maps/mainAssetsTest.map"
        CACHE INTERNAL "")

find_program(PYTHON3_BIN python3 REQUIRED)

# --- Achievements (shared, empty, one pair for every example) ---------------
set(EXAMPLES_ACHV_GEN_DIR "${CMAKE_BINARY_DIR}/generated/examples/achievements" CACHE INTERNAL "")
set(EXAMPLES_ACHV_JSON "${CMAKE_CURRENT_SOURCE_DIR}/config/achievements.json")
set(ACHV_GEN_PY "${CMAKE_SOURCE_DIR}/tools/achievements.py")

add_custom_command(
        OUTPUT "${EXAMPLES_ACHV_GEN_DIR}/AchievementIds.h"
        "${EXAMPLES_ACHV_GEN_DIR}/AchievementTable.cpp"
        COMMAND ${PYTHON3_BIN} "${ACHV_GEN_PY}"
        --declaration "${EXAMPLES_ACHV_JSON}"
        --emit-ids "${EXAMPLES_ACHV_GEN_DIR}/AchievementIds.h"
        --emit-table "${EXAMPLES_ACHV_GEN_DIR}/AchievementTable.cpp"
        DEPENDS "${EXAMPLES_ACHV_JSON}" "${ACHV_GEN_PY}"
        COMMENT "Generating shared example achievements from examples/config/achievements.json"
        VERBATIM
)
set_source_files_properties(
        "${EXAMPLES_ACHV_GEN_DIR}/AchievementIds.h"
        "${EXAMPLES_ACHV_GEN_DIR}/AchievementTable.cpp"
        PROPERTIES GENERATED TRUE
)
add_custom_target(examples-achievements-generate
        DEPENDS "${EXAMPLES_ACHV_GEN_DIR}/AchievementIds.h" "${EXAMPLES_ACHV_GEN_DIR}/AchievementTable.cpp"
)

# --- Actions (shared ids, table per platform, one pair for every example) ---
set(EXAMPLES_ACTIONS_GEN_DIR "${CMAKE_BINARY_DIR}/generated/examples/actions" CACHE INTERNAL "")
set(EXAMPLES_ACTIONS_JSON "${CMAKE_CURRENT_SOURCE_DIR}/config/actions.json" CACHE INTERNAL "")
set(ACTIONS_GEN_PY "${CMAKE_SOURCE_DIR}/tools/actions.py")

add_custom_command(
        OUTPUT "${EXAMPLES_ACTIONS_GEN_DIR}/ActionIds.h"
        COMMAND ${PYTHON3_BIN} "${ACTIONS_GEN_PY}"
        --declaration "${EXAMPLES_ACTIONS_JSON}"
        --emit-ids "${EXAMPLES_ACTIONS_GEN_DIR}/ActionIds.h"
        DEPENDS "${EXAMPLES_ACTIONS_JSON}" "${ACTIONS_GEN_PY}"
        COMMENT "Generating shared example action ids from examples/config/actions.json"
        VERBATIM
)
set_source_files_properties("${EXAMPLES_ACTIONS_GEN_DIR}/ActionIds.h" PROPERTIES GENERATED TRUE)
add_custom_target(examples-actions-ids-generate DEPENDS "${EXAMPLES_ACTIONS_GEN_DIR}/ActionIds.h")

# Called once per active platform. Reuses the ACTIONS_CAPS_<platform> paths
# game/CMakeLists.txt already declared CACHE INTERNAL -- the capability files
# are a property of the platform, not of which declaration is being resolved.
function(example_add_action_table PLATFORM)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_out "${EXAMPLES_ACTIONS_GEN_DIR}/${_lower}/ActionTable.cpp")

    # Shared by every example on this platform (one action declaration), but
    # this function runs once per example -- guard so the second example
    # doesn't redefine the first's target.
    if(TARGET examples-actions-table-generate-${_lower})
        return()
    endif()

    add_custom_command(
            OUTPUT "${_out}"
            COMMAND ${PYTHON3_BIN} "${ACTIONS_GEN_PY}"
            --declaration "${EXAMPLES_ACTIONS_JSON}"
            --project-root "${CMAKE_SOURCE_DIR}"
            --platform "${_lower}"
            --emit-table "${_out}"
            DEPENDS "${EXAMPLES_ACTIONS_JSON}" "${ACTIONS_GEN_PY}" "${ACTIONS_CAPS_${_lower}}"
            COMMENT "Generating example action table for ${_lower}"
            VERBATIM
    )
    set_source_files_properties("${_out}" PROPERTIES GENERATED TRUE)
    add_custom_target(examples-actions-table-generate-${_lower} DEPENDS "${_out}")
endfunction()

# --- Stage 3: cook (per platform, per example) -------------------------------
# Not shared with game/'s own cook: each example's dist tree is self-contained,
# same reasoning docs/PIPELINE.md gives for cooking being per platform in the
# first place. Reuses the game's shared source asset trees and theme -- an
# example authors its own art only when the example is specifically about it.
function(example_add_cook_target NAME PLATFORM)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_cooked "${CMAKE_SOURCE_DIR}/examples/dist/${NAME}/${_lower}/cooked")

    add_custom_target(cook-example-${NAME}-${_lower}
            COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/cook_assets.py"
            --platform "${_lower}"
            --src "${GAME_ASSETS_TEXTURES_SRC_DIR}"
            --src "${GAME_ASSETS_MODELS_SRC_DIR}"
            --src "${GAME_ASSETS_MATERIALS_SRC_DIR}"
            --src "${ENGINE_ASSETS_SRC_DIR}"
            --themes "${CMAKE_SOURCE_DIR}/game/config/theme.json"
            --dst "${_cooked}/rassets"
            COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/cook_shaders.py"
            --platform "${_lower}"
            --src "${ENGINE_ASSETS_SRC_DIR}/shaders"
            --dst "${_cooked}/shaders"
            COMMENT "Cook: examples/${NAME} assets -> examples/dist/${NAME}/${_lower}/cooked/rassets"
            VERBATIM
    )

    # Gated on validation, same as game/'s package-<platform>: a bad cook must
    # not reach a staged example.
    add_custom_target(package-example-${NAME}-${_lower}
            COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/validate_cooked.py"
            --platform "${_lower}"
            --dir "${_cooked}/rassets"
            COMMENT "Package: validate examples/dist/${NAME}/${_lower}/cooked/rassets"
            VERBATIM
    )
    add_dependencies(package-example-${NAME}-${_lower} cook-example-${NAME}-${_lower})
endfunction()

# --- Stage 3b: compile levels (per platform, per example that needs one) ----
# Mirrors game_add_cook_target()'s own compile-levels-<platform> target one
# level down: an example names which of assets/maps/*.map it actually loads
# (EXAMPLE_<name>_LEVEL_MAPS, set beside its SOURCES/INCLUDE_DIR above) rather
# than compiling every map in the tree, the same "only what this example
# needs" scoping example_add_cook_target already gives its cooked rassets.
# Only called (see the root CMakeLists.txt's per-platform loop) for an
# example that actually declares EXAMPLE_<name>_LEVEL_MAPS -- primitives and
# ui_gallery load no level and are untouched by this function existing.
function(example_add_level_compile_target NAME PLATFORM)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_cooked "${CMAKE_SOURCE_DIR}/examples/dist/${NAME}/${_lower}/cooked")

    add_custom_target(compile-levels-example-${NAME}-${_lower}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_cooked}/levels"
            COMMENT "Compiling levels: examples/${NAME} assets/maps -> examples/dist/${NAME}/${_lower}/cooked/levels"
    )
    foreach (MAP_FILE ${EXAMPLE_${NAME}_LEVEL_MAPS})
        add_custom_command(TARGET compile-levels-example-${NAME}-${_lower} POST_BUILD
                COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/compile_level.py"
                "${MAP_FILE}" --out "${_cooked}/levels"
                --textures "${GAME_ASSETS_TEXTURES_SRC_DIR}"
                --models "${GAME_ASSETS_MODELS_SRC_DIR}"
                --materials "${GAME_ASSETS_MATERIALS_SRC_DIR}"
                --platform "${_lower}"
                VERBATIM
        )
    endforeach ()

    # Same gate as cook-example-*: a level must be compiled before a staged
    # example can be considered validated/packaged.
    add_dependencies(package-example-${NAME}-${_lower} compile-levels-example-${NAME}-${_lower})
endfunction()

# --- Per-platform, per-example executable ------------------------------------
function(example_add_executable NAME PLATFORM ENGINE_TARGET OUT_TARGET OUT_DIST_DIR)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_target "app_${_lower}_example_${NAME}")
    set(_dist "${CMAKE_SOURCE_DIR}/examples/dist/${NAME}/${_lower}")

    example_add_action_table(${PLATFORM})
    set(_actionsTable "${EXAMPLES_ACTIONS_GEN_DIR}/${_lower}/ActionTable.cpp")

    # GENERATED is directory-scoped; re-assert it here since this function
    # runs from the root scope, which never saw the custom command that
    # produced these two files (the same reason game_add_executable() does).
    set_source_files_properties("${EXAMPLES_ACHV_GEN_DIR}/AchievementTable.cpp" PROPERTIES GENERATED TRUE)
    set_source_files_properties("${_actionsTable}" PROPERTIES GENERATED TRUE)

    # An example that loads a level (EXAMPLE_<name>_LEVEL_MAPS declared -- see
    # example_add_level_compile_target above) needs the same generated ECS
    # dispatch game/CMakeLists.txt's own ecs-generate target already produces
    # at ECS_GEN_DIR: game/ and every such example share tools/ECS/ECS.json's
    # generated EcsComponents.h/EcsSpawn.cpp (that generator's output is not
    # game-specific), but each links its own definitions of the
    # Game_Spawn_<classname> hooks EcsSpawn.cpp declares (this example's own
    # src/EcsHooks.cpp, mirroring game/src/EcsHooks.cpp) -- a missing one is a
    # link error on purpose. Examples with no level (primitives, ui_gallery)
    # never reference Ecs_SpawnDispatch and skip all of this.
    set(_extraSources "")
    set(_extraIncludes "")
    set(_extraDeps "")
    if(DEFINED EXAMPLE_${NAME}_LEVEL_MAPS)
        set_source_files_properties("${ECS_GEN_DIR}/EcsSpawn.cpp" PROPERTIES GENERATED TRUE)
        list(APPEND _extraSources "${ECS_GEN_DIR}/EcsSpawn.cpp")
        set(_extraIncludes "${ECS_GEN_DIR}")
        set(_extraDeps "ecs-generate")
    endif()

    add_executable(${_target} EXCLUDE_FROM_ALL ${EXAMPLE_${NAME}_SOURCES}
            "${EXAMPLES_ACHV_GEN_DIR}/AchievementTable.cpp"
            "${_actionsTable}"
            ${_extraSources})

    set_target_properties(${_target} PROPERTIES
            LINKER_LANGUAGE CXX
            OUTPUT_NAME "${PLATFORM_${PLATFORM}_EXE}"
            SUFFIX ""
            RUNTIME_OUTPUT_DIRECTORY "${_dist}"
    )

    target_include_directories(${_target} PUBLIC "${EXAMPLE_${NAME}_INCLUDE_DIR}")
    target_include_directories(${_target} PRIVATE "${EXAMPLES_ACHV_GEN_DIR}" "${EXAMPLES_ACTIONS_GEN_DIR}" ${_extraIncludes})
    target_link_libraries(${_target} PRIVATE ${ENGINE_TARGET})
    target_compile_options(${_target} PRIVATE -Wall -Wextra -Werror)
    add_dependencies(${_target} examples-achievements-generate examples-actions-ids-generate examples-actions-table-generate-${_lower} package-example-${NAME}-${_lower} ${_extraDeps})

    set(${OUT_TARGET} "${_target}" PARENT_SCOPE)
    set(${OUT_DIST_DIR} "${_dist}" PARENT_SCOPE)
endfunction()

# --- Staging ------------------------------------------------------------------
# Deliberately not platform_package()/platform_run(): those are wired for
# exactly one product per platform per configure (a single fixed target name,
# and on PS2/Vita/PSP a single hardcoded game/config/... identity), so calling
# them a second time per example collides. Making an installable per-example
# container (a PS2 ISO, a Vita VPK, a PSP EBOOT) needs those fragments
# parameterized by product first -- tracked in
# docs/backlog/testbed_examples.md. Every example still gets a real, linked,
# per-platform executable and validated cooked assets; this just stages them
# into a runnable folder the same way Win32's own platform_package() does.
#
# A platform whose executable cannot run from a staged folder at all defines
# the optional platform_example_package() hook, which appends its container
# build to the staging target. See docs/guidelines/NEW_PLATFORM.md.
function(example_stage PLATFORM NAME EXE_TARGET DIST_DIR)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_cooked "${CMAKE_SOURCE_DIR}/examples/dist/${NAME}/${_lower}/cooked")

    add_custom_target(stage-example-${NAME}-${_lower}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
            COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/pack_master_archive.py"
            --rassets "${_cooked}/rassets"
            --prefix RASSETS
            --levels "${_cooked}/levels"
            --dst "${DIST_DIR}/RASSETS.PS2R"
            COMMENT "Staging examples/dist/${NAME}/${_lower}/ (exe + assets)"
            VERBATIM
    )
    add_dependencies(stage-example-${NAME}-${_lower} ${EXE_TARGET} package-example-${NAME}-${_lower})

    if(COMMAND platform_example_package)
        platform_example_package(${PLATFORM} ${NAME} stage-example-${NAME}-${_lower} ${EXE_TARGET} "${DIST_DIR}")
    endif()
endfunction()
