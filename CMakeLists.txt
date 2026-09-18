cmake_minimum_required(VERSION 3.10)
project(pantheon C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DEBUG "Enable debug features" ON)
if(DEBUG)
    add_definitions(-DDEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Decides ENGINE_ACTIVE_PLATFORMS from PLATFORMS_TO_SUPPORT and the toolchain.
include(Platforms)

# --- Platform fragments -----------------------------------------------------
# Loaded before any engine target, so a platform can pull in its own third-party
# dependencies first. Variants share a base fragment, so include each once.
#
# Each fragment defines platform_configure(), plus optional platform_dependencies(),
# platform_package(), platform_run() and platform_debug_symbols(). Fixed names,
# because a configure only ever has one toolchain and therefore one base fragment.
set(_loadedFragments "")
foreach(PLATFORM ${ENGINE_ACTIVE_PLATFORMS})
    if(PLATFORM_${PLATFORM}_BASE STREQUAL "")
        set(_fragment "engine/config/${PLATFORM_${PLATFORM}_DIR}/platform.cmake")
    else()
        set(_fragment "engine/config/${PLATFORM_${PLATFORM}_BASE}/platform.cmake")
    endif()

    if(NOT "${_fragment}" IN_LIST _loadedFragments)
        include("${CMAKE_SOURCE_DIR}/${_fragment}")
        list(APPEND _loadedFragments "${_fragment}")
        if(COMMAND platform_dependencies)
            platform_dependencies()
        endif()
    endif()
endforeach()

add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(examples)

# --- One library, one executable and one bundle per active platform ---------
foreach(PLATFORM ${ENGINE_ACTIVE_PLATFORMS})
    platform_configure(${PLATFORM})

    engine_add_library(${PLATFORM} _engineTarget)

    # Cook and package are per platform, because what a platform wants cooked
    # differs. Declared before the executable, which depends on them.
    game_add_cook_target(${PLATFORM})
    game_add_executable(${PLATFORM} ${_engineTarget} _appTarget _distDir)

    # Targets this platform's executable must wait on, declared by its fragment.
    if(DEFINED PLATFORM_${PLATFORM}_LINK_DEPS)
        add_dependencies(${_appTarget} ${PLATFORM_${PLATFORM}_LINK_DEPS})
    endif()
    if(COMMAND platform_package)
        platform_package(${PLATFORM} ${_appTarget} "${_distDir}")
    endif()
    if(COMMAND platform_run)
        platform_run(${PLATFORM} ${_appTarget} "${_distDir}")
    endif()
    if(COMMAND platform_debug_symbols)
        platform_debug_symbols(${PLATFORM} ${_appTarget} "${_distDir}")
    endif()

    message(STATUS "  ${PLATFORM}: ${_engineTarget} -> dist/${PLATFORM_${PLATFORM}_DIST}/${PLATFORM_${PLATFORM}_EXE}")

    # Every registered example, for this same platform, linking the same
    # engine library. Never touches PLATFORM_<P>_PACKAGE_TARGET or the `dist`
    # target below -- examples build on request via `examples`, not as part
    # of the shipping path. See examples/CMakeLists.txt.
    foreach(_EXAMPLE ${EXAMPLE_NAMES})
        example_add_cook_target(${_EXAMPLE} ${PLATFORM})
        example_add_executable(${_EXAMPLE} ${PLATFORM} ${_engineTarget} _exampleTarget _exampleDistDir)
        if(DEFINED PLATFORM_${PLATFORM}_LINK_DEPS)
            add_dependencies(${_exampleTarget} ${PLATFORM_${PLATFORM}_LINK_DEPS})
        endif()
        example_stage(${PLATFORM} ${_EXAMPLE} ${_exampleTarget} "${_exampleDistDir}")
    endforeach()
endforeach()

# `cmake --build . --target examples` builds every example for every active
# platform, staged and runnable, without touching the game's own `dist`.
add_custom_target(examples)
foreach(PLATFORM ${ENGINE_ACTIVE_PLATFORMS})
    string(TOLOWER "${PLATFORM}" _lower)
    foreach(_EXAMPLE ${EXAMPLE_NAMES})
        add_dependencies(examples stage-example-${_EXAMPLE}-${_lower})
    endforeach()
endforeach()

# `cmake --build . --target dist` builds every active platform end to end.
add_custom_target(dist)
foreach(PLATFORM ${ENGINE_ACTIVE_PLATFORMS})
    string(TOLOWER "${PLATFORM}" _lower)
    add_dependencies(dist app_${_lower})
    # Whichever packaging target that platform declared (ISO, staged bundle...).
    if(DEFINED PLATFORM_${PLATFORM}_PACKAGE_TARGET AND TARGET ${PLATFORM_${PLATFORM}_PACKAGE_TARGET})
        add_dependencies(dist ${PLATFORM_${PLATFORM}_PACKAGE_TARGET})
    endif()
endforeach()

# --- Housekeeping -----------------------------------------------------------
# Extra paths come from the active platforms, so the root names none of them.
set(_extraClean "")
foreach(PLATFORM ${ENGINE_ACTIVE_PLATFORMS})
    if(DEFINED PLATFORM_${PLATFORM}_CLEAN_PATHS)
        list(APPEND _extraClean ${PLATFORM_${PLATFORM}_CLEAN_PATHS})
    endif()
endforeach()
list(REMOVE_DUPLICATES _extraClean)

add_custom_target(test-tools
    COMMAND ${PYTHON3_BIN} -m pytest "${CMAKE_SOURCE_DIR}/tools/tests" -q
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Running the tool test suite"
    VERBATIM
)

add_custom_target(clean-all
    COMMENT "Cleaning all build artefacts for a full from-scratch rebuild..."
    COMMAND ${CMAKE_MAKE_PROGRAM} clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_extraClean} "${CMAKE_SOURCE_DIR}/dist"
    VERBATIM
)
