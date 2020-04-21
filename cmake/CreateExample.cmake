include(CMakeParseArguments)

function(CreateExample)
set(USE_GLFW true)
set(USE_ASSIMP true)
set(USE_FT2 true)

cmake_parse_arguments(CE "NO_GLFW;NO_ASSIMP;NO_FT2" "DIR" "FILES" ${ARGN} )

if(CE_NO_GLFW)
    set(USE_GLFW false)
endif()

if(CE_NO_ASSIMP)
    set(USE_ASSIMP false)
endif()

if(CE_NO_FT2)
    set(USE_FT2 false)
endif()

set(SOURCE_FILE_LIST "")

foreach(f ${CE_FILES})
    list(APPEND SOURCE_FILE_LIST "${CMAKE_SOURCE_DIR}/example/${CE_DIR}/${f}")
endforeach()

set(BUILD_NAME "${CE_DIR}")

message(---------------------------------)
message(name = ${BUILD_NAME})
message(use_glfw = ${USE_GLFW})
message(use_assimp = ${USE_ASSIMP})
message(files = ${SOURCE_FILE_LIST})
message(---------------------------------)

add_executable(${BUILD_NAME} ${SOURCE_FILE_LIST}  glad.c ${ALL_SOURCE_FILES})

if(USE_GLFW)
    target_link_libraries(${BUILD_NAME} ${GLFW3_LIBRARY})
endif(USE_GLFW)

if(USE_ASSIMP)
    target_link_libraries(${BUILD_NAME} ${ASSIMP_LIBRARY})
endif(USE_ASSIMP)

if(USE_FT2)
    target_link_libraries(${BUILD_NAME} ${FREE_TYPE_LIBRARY})
endif(USE_FT2)


endfunction(CreateExample)
