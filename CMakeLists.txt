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

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR} 
)

if(GLM_FOUND AND GLFW3_FOUND)
    message(${GLM_INCLUDE_DIR})

    include_directories( ${GLFW3_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/3rd_party/stb
    ${GLM_INCLUDE_DIR} )

    add_executable(d1 d1/main.cpp  glad.c src/RenderDemo.cpp)

    target_link_libraries(d1 ${GLFW3_LIBRARY} )

endif()

        
