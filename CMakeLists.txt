cmake_minimum_required(VERSION 3.14)

# Ninja discovers MSVC header dependencies by parsing /showIncludes output.
# Keep CMake's compiler detection and later standalone builds on the same tool
# locale; an encoding mismatch can otherwise produce object files with zero
# tracked header dependencies.
if(WIN32 AND CMAKE_GENERATOR MATCHES "Ninja")
    set(ENV{VSLANG} "1033")
endif()

project("OpenGL demo" VERSION 0.1.0)

if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(_gld_msvc_english_env
        "${CMAKE_COMMAND}" -E env VSLANG=1033)

    if(CMAKE_C_COMPILER_LAUNCHER)
        list(APPEND _gld_msvc_english_env_c ${CMAKE_C_COMPILER_LAUNCHER})
        list(PREPEND _gld_msvc_english_env_c
            "${CMAKE_COMMAND}" -E env VSLANG=1033)
        set(CMAKE_C_COMPILER_LAUNCHER "${_gld_msvc_english_env_c}")
    else()
        set(CMAKE_C_COMPILER_LAUNCHER "${_gld_msvc_english_env}")
    endif()

    if(CMAKE_CXX_COMPILER_LAUNCHER)
        list(APPEND _gld_msvc_english_env_cxx ${CMAKE_CXX_COMPILER_LAUNCHER})
        list(PREPEND _gld_msvc_english_env_cxx
            "${CMAKE_COMMAND}" -E env VSLANG=1033)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${_gld_msvc_english_env_cxx}")
    else()
        set(CMAKE_CXX_COMPILER_LAUNCHER "${_gld_msvc_english_env}")
    endif()
endif()

include(CTest)

option(GLD_ENABLE_PERFORMANCE_MONITORING
    "Compile detailed CPU/GPU performance diagnostics (FPS is always enabled)"
    OFF)
if(GLD_ENABLE_PERFORMANCE_MONITORING)
    add_compile_definitions(GLD_ENABLE_PERFORMANCE_MONITORING=1)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(WIN32)

    message("is Windows")
    add_definitions(/permissive-)
    add_compile_options(/utf-8)
    add_definitions(-DPF_WIN32)
    if("${CMAKE_BUILD_TYPE}" STREQUAL "Release" )
        add_link_options(/SAFESEH:NO /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /NODEFAULTLIB:library)
    else()
        add_link_options(/SAFESEH:NO /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup /NODEFAULTLIB:library)
    endif()
    message("${CMAKE_EXE_LINKER_FLAGS}")
else()

if(APPLE) 
    SET(CMAKE_CXX_COMPILE_FEATURES "${CMAKE_CXX_COMPILE_FEATURES} -std=c++17")
    add_definitions(-DPF_APPLE)
endif()

endif()

include(cmake/CreateExample.cmake)

find_package(GLM)
find_package(GLFW3)
find_package(MINI_TEST)
find_package(ASSIMP)
find_package(FT2PP)
find_package(FREE_TYPE)
find_package(SPY)
find_package(BASS24)
find_package(EnTT CONFIG REQUIRED)

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

set(GLD_PLATFORM_LIBS "")
if(WIN32)
    list(APPEND GLD_PLATFORM_LIBS Ole32 Mmdevapi)
endif()

add_definitions(-DNOMINMAX)
add_definitions(-DGLM_ENABLE_EXPERIMENTAL)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

if(MINI_TEST_FOUND)
    include_directories(${MINI_TEST_INCLUDE_DIR})
endif()

if(FT2PP_FOUND AND FREE_TYPE_FOUND)
    include_directories(${FT2PP_INCLUDE_DIR} ${FREE_TYPE_INCLUDE_DIR})
endif()

if(SPY_FOUND )
    include_directories(${SPY_INCLUDE_DIR})
endif()

if(GLM_FOUND AND GLFW3_FOUND)
    message(${GLM_INCLUDE_DIR})

    if(GLD_PLATFORM_LIBS)
        link_libraries(${GLD_PLATFORM_LIBS})
    endif()

    include_directories( ${GLFW3_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/3rd_party/stb
    ${GLM_INCLUDE_DIR} 
    ${ASSIMP_INCLUDE_DIR}
    )

    aux_source_directory("src" ALL_SOURCE_FILES)

    add_library(gld_aoe2 STATIC
        aoe2/src/Aoe2Assets.cpp
        aoe2/src/Aoe2ResourceManager.cpp
        aoe2/src/Aoe2Systems.cpp
        aoe2/src/Aoe2Batch.cpp
        aoe2/src/Aoe2Plugin.cpp)
    target_include_directories(gld_aoe2 PUBLIC ${CMAKE_SOURCE_DIR}/aoe2/include)
    target_link_libraries(gld_aoe2 PUBLIC nlohmann_json::nlohmann_json EnTT::EnTT)

    add_library(gld_aoe_map STATIC aoe/src/AoeMap.cpp)
    target_include_directories(gld_aoe_map PUBLIC ${CMAKE_SOURCE_DIR}/aoe/include)
    target_link_libraries(gld_aoe_map PUBLIC nlohmann_json::nlohmann_json EnTT::EnTT)

    add_library(gld_aoe_gameplay STATIC
        aoe/src/AoeGameplay.cpp
        aoe/src/AoeCombat.cpp
        aoe/src/AoeFormation.cpp
        aoe/src/AoeFormationFallback.cpp
        aoe/src/AoeGlobalMotion.cpp
        aoe/src/AoeGlobalMotionFallback.cpp
        aoe/src/AoeLocalAvoidance.cpp
        aoe/src/AoeLocalAvoidanceFallback.cpp)
    target_include_directories(gld_aoe_gameplay PUBLIC ${CMAKE_SOURCE_DIR}/aoe/include)
    target_link_libraries(gld_aoe_gameplay PUBLIC gld_aoe_map nlohmann_json::nlohmann_json EnTT::EnTT)
    if(MSVC)
        target_compile_options(gld_aoe_gameplay PRIVATE /bigobj)
    endif()

    add_library(gld_aoe2x_pathfinding STATIC
        aoe2x/src/Aoe2xPathfinding.cpp
        aoe2x/src/Aoe2xFormation.cpp)
    target_include_directories(gld_aoe2x_pathfinding PUBLIC
        ${CMAKE_SOURCE_DIR}/aoe/include ${CMAKE_SOURCE_DIR}/aoe2x/include)
    target_link_libraries(gld_aoe2x_pathfinding PUBLIC gld_aoe_map EnTT::EnTT)

    # OpenGL 4.3 image-based global motion planner. Keeping this target
    # separate preserves renderer-free/headless gld_aoe_gameplay tests.
    add_library(gld_aoe_gpu_motion STATIC
        aoe_gpu_motion/src/AoeGpuMotion.cpp)
    target_include_directories(gld_aoe_gpu_motion PUBLIC
        ${CMAKE_SOURCE_DIR}/aoe_gpu_motion/include)
    target_link_libraries(gld_aoe_gpu_motion PUBLIC gld_aoe_gameplay)

    add_library(gld_aoe2_gameplay_bridge STATIC
        aoe2_gameplay/src/Aoe2GameplayBridge.cpp)
    target_include_directories(gld_aoe2_gameplay_bridge PUBLIC
        ${CMAKE_SOURCE_DIR}/aoe2_gameplay/include)
    target_link_libraries(gld_aoe2_gameplay_bridge PUBLIC gld_aoe_gameplay gld_aoe2)
    if(BUILD_TESTING)
        add_executable(ecs_world_tests aoe2/tests/EcsWorldTests.cpp)
        target_link_libraries(ecs_world_tests PRIVATE EnTT::EnTT)
        add_test(NAME ecs_world_tests COMMAND ecs_world_tests)

        add_executable(aoe2_runtime_tests aoe2/tests/Aoe2RuntimeTests.cpp)
        # The tests assert diagnostic traversal counts. Keep those assertions
        # active without forcing monitoring overhead into normal executables.
        target_compile_definitions(aoe2_runtime_tests PRIVATE
            GLD_ENABLE_PERFORMANCE_MONITORING=1)
        target_include_directories(aoe2_runtime_tests PRIVATE ${CMAKE_SOURCE_DIR}/aoe2/include)
        target_link_libraries(aoe2_runtime_tests PRIVATE gld_aoe2 EnTT::EnTT)
        add_test(NAME aoe2_runtime_tests COMMAND aoe2_runtime_tests)
        add_executable(gizmo_tests
            aoe2/tests/GizmoTests.cpp
            src/EcsGizmoGeometry.cpp)
        target_link_libraries(gizmo_tests PRIVATE EnTT::EnTT)
        add_test(NAME gizmo_tests COMMAND gizmo_tests)
        add_executable(aoe_gameplay_tests aoe/tests/AoeGameplayTests.cpp)
        target_link_libraries(aoe_gameplay_tests PRIVATE gld_aoe_gameplay)
        add_test(NAME aoe_gameplay_tests COMMAND aoe_gameplay_tests)
        add_executable(aoe_map_tests aoe/tests/AoeMapTests.cpp)
        target_link_libraries(aoe_map_tests PRIVATE gld_aoe_gameplay)
        add_test(NAME aoe_map_tests COMMAND aoe_map_tests)
        add_executable(aoe2x_gameplay_system_tests
            aoe2x/tests/Aoe2xGameplaySystemTests.cpp)
        target_include_directories(aoe2x_gameplay_system_tests PRIVATE
            ${CMAKE_SOURCE_DIR}/aoe/include ${CMAKE_SOURCE_DIR}/aoe2x/include)
        target_link_libraries(aoe2x_gameplay_system_tests PRIVATE EnTT::EnTT)
        add_test(NAME aoe2x_gameplay_system_tests
            COMMAND aoe2x_gameplay_system_tests)
        add_executable(aoe2x_gameplay_system_profile_tests
            aoe2x/tests/Aoe2xGameplaySystemTests.cpp)
        target_compile_definitions(aoe2x_gameplay_system_profile_tests PRIVATE
            GLD_ENABLE_PERFORMANCE_MONITORING=1)
        target_include_directories(aoe2x_gameplay_system_profile_tests PRIVATE
            ${CMAKE_SOURCE_DIR}/aoe/include ${CMAKE_SOURCE_DIR}/aoe2x/include)
        target_link_libraries(aoe2x_gameplay_system_profile_tests PRIVATE EnTT::EnTT)
        add_test(NAME aoe2x_gameplay_system_profile_tests
            COMMAND aoe2x_gameplay_system_profile_tests)
        add_executable(aoe2x_pathfinding_tests aoe2x/tests/Aoe2xPathfindingTests.cpp)
        target_include_directories(aoe2x_pathfinding_tests PRIVATE
            ${CMAKE_SOURCE_DIR}/aoe/include ${CMAKE_SOURCE_DIR}/aoe2x/include)
        target_link_libraries(aoe2x_pathfinding_tests PRIVATE gld_aoe2x_pathfinding)
        add_test(NAME aoe2x_pathfinding_tests COMMAND aoe2x_pathfinding_tests)
        add_executable(aoe2x_formation_tests aoe2x/tests/Aoe2xFormationTests.cpp)
        target_include_directories(aoe2x_formation_tests PRIVATE
            ${CMAKE_SOURCE_DIR}/aoe/include ${CMAKE_SOURCE_DIR}/aoe2x/include)
        target_link_libraries(aoe2x_formation_tests PRIVATE gld_aoe2x_pathfinding)
        add_test(NAME aoe2x_formation_tests COMMAND aoe2x_formation_tests)
        add_executable(aoe_map_benchmark example/aoe_map_benchmark/main.cpp)
        target_link_libraries(aoe_map_benchmark PRIVATE gld_aoe_gameplay)
        add_executable(aoe2_gameplay_bridge_tests
            aoe2_gameplay/tests/Aoe2GameplayBridgeTests.cpp)
        target_link_libraries(aoe2_gameplay_bridge_tests PRIVATE
            gld_aoe2_gameplay_bridge)
        add_test(NAME aoe2_gameplay_bridge_tests COMMAND aoe2_gameplay_bridge_tests)
    endif()

    message(${ALL_SOURCE_FILES})

    add_executable(d1 d1/main.cpp  glad.c ${ALL_SOURCE_FILES})

    target_link_libraries(d1 ${GLFW3_LIBRARY} ${ASSIMP_LIBRARY} ${FREE_TYPE_LIBRARY})

    add_executable(lighting_1 lighting/lighting_1.cpp  glad.c ${ALL_SOURCE_FILES})

    target_link_libraries(lighting_1 ${GLFW3_LIBRARY} ${ASSIMP_LIBRARY})

    add_executable(lighting_2 lighting_2/main.cpp  glad.c ${ALL_SOURCE_FILES})

    target_link_libraries(lighting_2 ${GLFW3_LIBRARY} ${ASSIMP_LIBRARY})

    add_executable(lighting_3 lighting_3/main.cpp  glad.c ${ALL_SOURCE_FILES})

    target_link_libraries(lighting_3 ${GLFW3_LIBRARY} ${ASSIMP_LIBRARY})

    add_executable(lighting_4 lighting_4/main.cpp  glad.c ${ALL_SOURCE_FILES})

    target_link_libraries(lighting_4 ${GLFW3_LIBRARY} ${ASSIMP_LIBRARY})

    add_executable(lighting_5 lighting_5/main.cpp  glad.c ${ALL_SOURCE_FILES})

    target_link_libraries(lighting_5 ${GLFW3_LIBRARY} ${ASSIMP_LIBRARY})

    add_executable(lighting_6 lighting_6/main.cpp  glad.c ${ALL_SOURCE_FILES})

    target_link_libraries(lighting_6 ${GLFW3_LIBRARY} ${ASSIMP_LIBRARY})

    add_executable(load_model load_model/main.cpp  glad.c ${ALL_SOURCE_FILES})

    target_link_libraries(load_model ${GLFW3_LIBRARY} ${ASSIMP_LIBRARY})

    add_executable(stencilt_test stencilt_test/main.cpp  glad.c ${ALL_SOURCE_FILES})

    target_link_libraries(stencilt_test ${GLFW3_LIBRARY} ${ASSIMP_LIBRARY} ${FREE_TYPE_LIBRARY})

    CreateExample(DIR framebuffer FILES main.cpp)
    CreateExample(DIR skybox FILES main.cpp)
    CreateExample(DIR geometry FILES main.cpp)
    CreateExample(DIR reflect FILES main.cpp)
    CreateExample(DIR refract FILES main.cpp)
    CreateExample(DIR heart FILES main.cpp)
    CreateExample(DIR instantiation FILES main.cpp)
    CreateExample(DIR blinn FILES main.cpp)
    CreateExample(DIR shadow FILES main.cpp)
    CreateExample(DIR shadow2 FILES main.cpp)
    CreateExample(DIR clock FILES main.cpp)
    CreateExample(DIR line FILES main.cpp)
    CreateExample(DIR test_curved FILES main.cpp)
    CreateExample(DIR clock2 FILES main.cpp)
    CreateExample(DIR word FILES main.cpp)
    CreateExample(DIR photo_viewer FILES main.cpp)
    CreateExample(DIR test_sphere FILES main.cpp)
    CreateExample(DIR text3d FILES main.cpp)
    CreateExample(DIR ecs_demo FILES main.cpp)
    CreateExample(DIR ecs_text FILES main.cpp)
    CreateExample(DIR ecs_text_fx FILES main.cpp)
    CreateExample(DIR ecs_multicam FILES main.cpp)
    CreateExample(DIR ecs_postprocess FILES main.cpp)
    CreateExample(DIR ecs_lighting FILES main.cpp)
    CreateExample(DIR ecs_audio_fft FILES main.cpp)
    CreateExample(DIR aoe2_unit_preview FILES main.cpp)
    target_link_libraries(aoe2_unit_preview gld_aoe2)

    CreateExample(DIR aoe2_unit_metadata_preview FILES main.cpp)
    target_link_libraries(aoe2_unit_metadata_preview gld_aoe2)

    CreateExample(DIR aoe2_unit_benchmark FILES main.cpp)
    target_link_libraries(aoe2_unit_benchmark gld_aoe2)

    CreateExample(DIR aoe_gameplay_unit_preview FILES main.cpp)
    target_link_libraries(aoe_gameplay_unit_preview gld_aoe_gpu_motion)
    target_link_libraries(aoe_gameplay_unit_preview gld_aoe2_gameplay_bridge)

    CreateExample(DIR aoe_gameplay_squad_preview FILES main.cpp)
    target_link_libraries(aoe_gameplay_squad_preview
        gld_aoe2_gameplay_bridge gld_aoe_gpu_motion)

    CreateExample(DIR aoe2x_pathfinding_preview FILES main.cpp)
    target_include_directories(aoe2x_pathfinding_preview PRIVATE
        ${CMAKE_SOURCE_DIR}/aoe/include ${CMAKE_SOURCE_DIR}/aoe2x/include)
    target_compile_definitions(aoe2x_pathfinding_preview PRIVATE
        GLD_ENABLE_PERFORMANCE_MONITORING=1)
    target_link_libraries(aoe2x_pathfinding_preview gld_aoe2x_pathfinding)

    CreateExample(DIR aoe2x_formation_preview FILES main.cpp)
    target_include_directories(aoe2x_formation_preview PRIVATE
        ${CMAKE_SOURCE_DIR}/aoe/include ${CMAKE_SOURCE_DIR}/aoe2x/include)
    target_compile_definitions(aoe2x_formation_preview PRIVATE
        GLD_ENABLE_PERFORMANCE_MONITORING=1)
    target_link_libraries(aoe2x_formation_preview gld_aoe2x_pathfinding)

    CreateExample(DIR music_player FILES main.cpp GetFileName.cpp MusicPlayer.cpp Pumper.cpp SupportFile.cpp MMFile.cpp 
        lrc/lrc_loader.cpp lrc/lrc_analyse.cpp lrc/lrc_view.cpp)
    target_include_directories(music_player PRIVATE ${BASS24_INCLUDE_DIR})
    target_link_libraries(music_player ${BASS24_LIBRARYS} )

endif()

        
