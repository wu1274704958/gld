cmake_minimum_required(VERSION 3.0.0)
project("OpenGL demo" VERSION 0.1.0)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 17)
if(WIN32)

    message("is Windows")
    add_definitions(/permissive-)
    if(${CMAKE_BUILD_TYPE} STREQUAL "Release" )
        add_link_options(/SAFESEH:NO /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /NODEFAULTLIB:library)
    else()
        add_link_options(/SAFESEH:NO /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup /NODEFAULTLIB:library)
    endif()
    message(${CMAKE_EXE_LINKER_FLAGS})
else()


endif()

find_package(GLM)
find_package(GLFW3)
find_package(MINI_TEST)
find_package(ASSIMP)

add_definitions(-DNOMINMAX)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR} 
)

if(MINI_TEST_FOUND)
    include_directories(${MINI_TEST_INCLUDE_DIR})
endif()

if(GLM_FOUND AND GLFW3_FOUND)
    message(${GLM_INCLUDE_DIR})

    include_directories( ${GLFW3_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/3rd_party/stb
    ${GLM_INCLUDE_DIR} 
    ${ASSIMP_INCLUDE_DIR}
    )

    aux_source_directory("src" ALL_SOURCE_FILES)

    message(${ALL_SOURCE_FILES})

    add_executable(d1 d1/main.cpp  glad.c ${ALL_SOURCE_FILES})

    target_link_libraries(d1 ${GLFW3_LIBRARY} ${ASSIMP_LIBRARY})

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

    target_link_libraries(stencilt_test ${GLFW3_LIBRARY} ${ASSIMP_LIBRARY})

endif()

        
