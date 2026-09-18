# ---------------------------------------------------------------------------
# The game.
#
# The sources and the asset pipeline are platform-neutral and defined once; the
# executable is created per active platform by game_add_executable(), each
# linking that platform engine library and landing in its own dist/ bundle.
# ---------------------------------------------------------------------------

set(GAME_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Game.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/EcsHooks.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/scenes/MainScene.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/scenes/MainMenu.cpp"
        CACHE INTERNAL "")

set(GAME_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/scenes/MainScene.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/scenes/MainMenu.h"
        CACHE INTERNAL "")

set(GAME_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include" CACHE INTERNAL "")

find_program(PYTHON3_BIN python3 REQUIRED)

# --- ECS code generation (shared by every platform) -------------------------
# tools/ECS/ECS.json is the single source of truth for the component/entity C++
# and the TrenchBroom FGD. Generating at build time makes drift impossible; the
# FGD is copied back into the committed editor profile so TrenchBroom stays
# current (copy_if_different = no-op on a clean tree).
set(ECS_GEN_DIR "${CMAKE_BINARY_DIR}/generated/ecs" CACHE INTERNAL "")
set(ECS_JSON "${CMAKE_SOURCE_DIR}/tools/ECS/ECS.json")
set(ECS_SCHEMA "${CMAKE_SOURCE_DIR}/tools/ECS/schema.json")
set(ECS_GEN_PY "${CMAKE_SOURCE_DIR}/tools/ECS/generate_ecs.py")
set(ECS_FGD_COMMITTED "${CMAKE_SOURCE_DIR}/tools/trenchbroom/games/Pantheon/Pantheon.fgd")

add_custom_command(
        OUTPUT "${ECS_GEN_DIR}/EcsComponents.h"
        "${ECS_GEN_DIR}/EcsSpawn.cpp"
        "${ECS_GEN_DIR}/Pantheon.fgd"
        COMMAND ${PYTHON3_BIN} "${ECS_GEN_PY}"
        --ecs "${ECS_JSON}" --schema "${ECS_SCHEMA}"
        --out-header "${ECS_GEN_DIR}/EcsComponents.h"
        --out-source "${ECS_GEN_DIR}/EcsSpawn.cpp"
        --out-fgd "${ECS_GEN_DIR}/Pantheon.fgd"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ECS_GEN_DIR}/Pantheon.fgd" "${ECS_FGD_COMMITTED}"
        DEPENDS "${ECS_JSON}" "${ECS_SCHEMA}" "${ECS_GEN_PY}"
        COMMENT "Generating ECS (EcsComponents.h, EcsSpawn.cpp, FGD) from ECS.json"
        VERBATIM
)
set_source_files_properties(
        "${ECS_GEN_DIR}/EcsComponents.h"
        "${ECS_GEN_DIR}/EcsSpawn.cpp"
        PROPERTIES GENERATED TRUE
)

# The app targets are created from the root scope, where a custom command OUTPUT
# in this directory is not visible as a dependency. A target bridges the two.
add_custom_target(ecs-generate
        DEPENDS "${ECS_GEN_DIR}/EcsComponents.h" "${ECS_GEN_DIR}/EcsSpawn.cpp"
)

# --- Achievement code generation (shared by every platform) -----------------
# game/config/achievements.json is the single source of truth for the set. The
# enum is what game code names an achievement by; the table is what the engine
# reads to show one. Generating at build time is what keeps the declaration,
# the engine and every platform's packaging describing one set.
set(ACHV_GEN_DIR "${CMAKE_BINARY_DIR}/generated/achievements" CACHE INTERNAL "")
set(ACHV_JSON "${CMAKE_CURRENT_SOURCE_DIR}/config/achievements.json")
set(ACHV_GEN_PY "${CMAKE_SOURCE_DIR}/tools/achievements.py")

add_custom_command(
        OUTPUT "${ACHV_GEN_DIR}/AchievementIds.h"
        "${ACHV_GEN_DIR}/AchievementTable.cpp"
        COMMAND ${PYTHON3_BIN} "${ACHV_GEN_PY}"
        --declaration "${ACHV_JSON}"
        --emit-ids "${ACHV_GEN_DIR}/AchievementIds.h"
        --emit-table "${ACHV_GEN_DIR}/AchievementTable.cpp"
        DEPENDS "${ACHV_JSON}" "${ACHV_GEN_PY}"
        COMMENT "Generating achievements (AchievementIds.h, AchievementTable.cpp) from achievements.json"
        VERBATIM
)
set_source_files_properties(
        "${ACHV_GEN_DIR}/AchievementIds.h"
        "${ACHV_GEN_DIR}/AchievementTable.cpp"
        PROPERTIES GENERATED TRUE
)

add_custom_target(achievements-generate
        DEPENDS "${ACHV_GEN_DIR}/AchievementIds.h" "${ACHV_GEN_DIR}/AchievementTable.cpp"
)

# --- Action code generation (ids shared, table per-platform) ----------------
# game/config/actions.json is the single source of truth. ActionId/
# ActionContextId are what game code names an action and a context by, and
# never vary by platform. The table does: which of an action's declared
# bindings actually compiles on a platform is a capability question (a
# missing L3 on Vita handheld, no keyboard on PS2), read from that platform's
# own engine/config/.../input_capabilities.json, so unlike every other
# generated pair in this file the table is generated once per active
# platform rather than once shared by all of them.
set(ACTIONS_GEN_DIR "${CMAKE_BINARY_DIR}/generated/actions" CACHE INTERNAL "")
set(ACTIONS_JSON "${CMAKE_CURRENT_SOURCE_DIR}/config/actions.json" CACHE INTERNAL "")
set(ACTIONS_GEN_PY "${CMAKE_SOURCE_DIR}/tools/actions.py" CACHE INTERNAL "")

# game_add_action_table() below is called from game_add_executable(), which
# in turn is called from the root CMakeLists.txt's per-platform foreach loop
# -- root scope, not this directory's. Non-cache variables set here are
# invisible there, so anything the function needs (these included) must be
# CACHE INTERNAL, the same as ACTIONS_GEN_DIR above.
set(ACTIONS_CAPS_ps2pal "${CMAKE_SOURCE_DIR}/engine/config/ps2/input_capabilities.json" CACHE INTERNAL "")
set(ACTIONS_CAPS_ps2ntsc "${CMAKE_SOURCE_DIR}/engine/config/ps2/input_capabilities.json" CACHE INTERNAL "")
set(ACTIONS_CAPS_win32 "${CMAKE_SOURCE_DIR}/engine/config/win32/input_capabilities.json" CACHE INTERNAL "")
set(ACTIONS_CAPS_vita "${CMAKE_SOURCE_DIR}/engine/config/vita/handheld/input_capabilities.json" CACHE INTERNAL "")
set(ACTIONS_CAPS_vitatv "${CMAKE_SOURCE_DIR}/engine/config/vita/tv/input_capabilities.json" CACHE INTERNAL "")
set(ACTIONS_CAPS_psp "${CMAKE_SOURCE_DIR}/engine/config/psp/input_capabilities.json" CACHE INTERNAL "")
set(ACTIONS_CAPS_nx "${CMAKE_SOURCE_DIR}/engine/config/nx/input_capabilities.json" CACHE INTERNAL "")

add_custom_command(
        OUTPUT "${ACTIONS_GEN_DIR}/ActionIds.h"
        COMMAND ${PYTHON3_BIN} "${ACTIONS_GEN_PY}"
        --declaration "${ACTIONS_JSON}"
        --emit-ids "${ACTIONS_GEN_DIR}/ActionIds.h"
        DEPENDS "${ACTIONS_JSON}" "${ACTIONS_GEN_PY}"
        COMMENT "Generating action ids (ActionIds.h) from actions.json"
        VERBATIM
)
set_source_files_properties("${ACTIONS_GEN_DIR}/ActionIds.h" PROPERTIES GENERATED TRUE)
add_custom_target(actions-ids-generate DEPENDS "${ACTIONS_GEN_DIR}/ActionIds.h")

# Called once per active platform, from game_add_executable() below.
function(game_add_action_table PLATFORM)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_out "${ACTIONS_GEN_DIR}/${_lower}/ActionTable.cpp")

    add_custom_command(
            OUTPUT "${_out}"
            COMMAND ${PYTHON3_BIN} "${ACTIONS_GEN_PY}"
            --declaration "${ACTIONS_JSON}"
            --project-root "${CMAKE_SOURCE_DIR}"
            --platform "${_lower}"
            --emit-table "${_out}"
            DEPENDS "${ACTIONS_JSON}" "${ACTIONS_GEN_PY}" "${ACTIONS_CAPS_${_lower}}"
            COMMENT "Generating action table for ${_lower} (bindings resolved against its input_capabilities.json)"
            VERBATIM
    )
    set_source_files_properties("${_out}" PROPERTIES GENERATED TRUE)
    add_custom_target(actions-table-generate-${_lower} DEPENDS "${_out}")
endfunction()

# --- Stage 3: cook (per platform) -------------------------------------------
# Cooking is NOT shared: the right encoding is a hardware question, so each
# platform bakes from its own cook list into its own tree. See docs/PIPELINE.md.
# Levels are cooked per platform too, for the same reason: a brush material's
# max size is a per-platform policy (cooklist.json's "level_textures"), not
# something one compiled level can share across every target.
#
# game_add_cook_target() is called once per surviving platform, below.
set(GAME_ASSETS_TEXTURES_SRC_DIR "${CMAKE_SOURCE_DIR}/assets/textures" CACHE INTERNAL "Source textures")
set(GAME_ASSETS_MODELS_SRC_DIR "${CMAKE_SOURCE_DIR}/assets/models" CACHE INTERNAL "Source models")

file(GLOB _level_maps CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/maps/*.map")
set(LEVEL_MAPS "${_level_maps}" CACHE INTERNAL "Level map sources")

function(game_add_cook_target PLATFORM)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_cooked "${CMAKE_SOURCE_DIR}/dist/cooked/${_lower}")

    add_custom_target(cook-${_lower}
            COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/cook_assets.py"
            --platform "${_lower}"
            --src "${GAME_ASSETS_TEXTURES_SRC_DIR}"
            --src "${GAME_ASSETS_MODELS_SRC_DIR}"
            --src "${ENGINE_ASSETS_SRC_DIR}"
            --themes "${CMAKE_SOURCE_DIR}/game/config/theme.json"
            --dst "${_cooked}/rassets"
            COMMENT "Cook: engine + game source assets -> dist/cooked/${_lower}/rassets"
            VERBATIM
    )

    add_custom_target(compile-levels-${_lower}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_cooked}/levels"
            COMMENT "Compiling levels from assets/maps/*.map -> dist/cooked/${_lower}/levels"
    )
    foreach (MAP_FILE ${LEVEL_MAPS})
        add_custom_command(TARGET compile-levels-${_lower} POST_BUILD
                COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/compile_level.py"
                "${MAP_FILE}" --out "${_cooked}/levels"
                --textures "${CMAKE_SOURCE_DIR}/assets/textures"
                --models "${CMAKE_SOURCE_DIR}/assets/models"
                --platform "${_lower}"
                VERBATIM
        )
    endforeach ()

    # Stage 4 is gated on validation, so a bad cook can never reach a container.
    add_custom_target(package-${_lower}
            COMMAND ${PYTHON3_BIN} "${CMAKE_SOURCE_DIR}/tools/validate_cooked.py"
            --platform "${_lower}"
            COMMENT "Package: validate dist/cooked/${_lower}, then build containers"
            VERBATIM
    )
    add_dependencies(package-${_lower} cook-${_lower} compile-levels-${_lower})
endfunction()

# --- Per-platform executable ------------------------------------------------
function(game_add_executable PLATFORM ENGINE_TARGET OUT_TARGET OUT_DIST_DIR)
    string(TOLOWER "${PLATFORM}" _lower)
    set(_target "app_${_lower}")
    set(_dist "${CMAKE_SOURCE_DIR}/dist/${PLATFORM_${PLATFORM}_DIST}")

    game_add_action_table(${PLATFORM})
    set(_actions_table "${ACTIONS_GEN_DIR}/${_lower}/ActionTable.cpp")

    # GENERATED is a directory-scoped property, so re-assert it here: this
    # function runs in the root scope, which has not seen the custom command.
    set_source_files_properties("${ECS_GEN_DIR}/EcsSpawn.cpp" PROPERTIES GENERATED TRUE)
    set_source_files_properties("${ACHV_GEN_DIR}/AchievementTable.cpp" PROPERTIES GENERATED TRUE)
    set_source_files_properties("${_actions_table}" PROPERTIES GENERATED TRUE)

    add_executable(${_target} EXCLUDE_FROM_ALL ${GAME_SOURCES}
            "${ECS_GEN_DIR}/EcsSpawn.cpp"
            "${ACHV_GEN_DIR}/AchievementTable.cpp"
            "${_actions_table}")

    # SUFFIX "" because PLATFORM_<P>_EXE is the complete filename: CMake would
    # otherwise append its own platform suffix and produce game.exe.exe.
    set_target_properties(${_target} PROPERTIES
            LINKER_LANGUAGE CXX
            OUTPUT_NAME "${PLATFORM_${PLATFORM}_EXE}"
            SUFFIX ""
            RUNTIME_OUTPUT_DIRECTORY "${_dist}"
    )

    target_include_directories(${_target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}../engine/include")
    target_include_directories(${_target} PRIVATE "${ECS_GEN_DIR}" "${ACHV_GEN_DIR}" "${ACTIONS_GEN_DIR}")
    target_link_libraries(${_target} PRIVATE ${ENGINE_TARGET})
    target_compile_options(${_target} PRIVATE -Wall -Wextra -Werror)
    add_dependencies(${_target} ecs-generate achievements-generate actions-ids-generate actions-table-generate-${_lower} package-${_lower})

    set(${OUT_TARGET} "${_target}" PARENT_SCOPE)
    set(${OUT_DIST_DIR} "${_dist}" PARENT_SCOPE)
endfunction()
