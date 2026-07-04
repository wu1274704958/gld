message( "finding GLFW3!"  )

set( GLFW3_FOUND FALSE )
set( GLFW3_TARGET "" )
set( GLFW3_LIBRARIES "" )

function(_GLFW3_COLLECT_VCPKG_INSTALLED_DIRS _OUT)
    set(_GLFW3_ROOTS)

    if(DEFINED VCPKG_ROOT)
        list(APPEND _GLFW3_ROOTS "${VCPKG_ROOT}")
    endif()

    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND _GLFW3_ROOTS "$ENV{VCPKG_ROOT}")
    endif()

    if(DEFINED CMAKE_TOOLCHAIN_FILE)
        get_filename_component(_GLFW3_TOOLCHAIN_DIR "${CMAKE_TOOLCHAIN_FILE}" PATH)
        get_filename_component(_GLFW3_SCRIPTS_DIR "${_GLFW3_TOOLCHAIN_DIR}" PATH)
        get_filename_component(_GLFW3_ROOT_FROM_TOOLCHAIN "${_GLFW3_SCRIPTS_DIR}" PATH)
        list(APPEND _GLFW3_ROOTS "${_GLFW3_ROOT_FROM_TOOLCHAIN}")
    endif()

    list(APPEND _GLFW3_ROOTS "F:/library/vcpkg")

    set(_GLFW3_DIRS)
    foreach(_GLFW3_ROOT ${_GLFW3_ROOTS})
        file(TO_CMAKE_PATH "${_GLFW3_ROOT}" _GLFW3_ROOT)
        if(EXISTS "${_GLFW3_ROOT}/installed")
            set(_GLFW3_TRIPLETS)

            if(DEFINED VCPKG_TARGET_TRIPLET)
                list(APPEND _GLFW3_TRIPLETS "${VCPKG_TARGET_TRIPLET}")
            endif()

            if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET})
                list(APPEND _GLFW3_TRIPLETS "$ENV{VCPKG_DEFAULT_TRIPLET}")
            endif()

            if(WIN32)
                if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                    list(APPEND _GLFW3_TRIPLETS x64-windows)
                else()
                    list(APPEND _GLFW3_TRIPLETS x86-windows)
                endif()
            endif()

            file(GLOB _GLFW3_INSTALLED_TRIPLETS RELATIVE "${_GLFW3_ROOT}/installed" "${_GLFW3_ROOT}/installed/*")
            list(APPEND _GLFW3_TRIPLETS ${_GLFW3_INSTALLED_TRIPLETS})

            if(_GLFW3_TRIPLETS)
                list(REMOVE_DUPLICATES _GLFW3_TRIPLETS)
            endif()

            foreach(_GLFW3_TRIPLET ${_GLFW3_TRIPLETS})
                if(EXISTS "${_GLFW3_ROOT}/installed/${_GLFW3_TRIPLET}")
                    list(APPEND _GLFW3_DIRS "${_GLFW3_ROOT}/installed/${_GLFW3_TRIPLET}")
                endif()
            endforeach()
        endif()
    endforeach()

    if(_GLFW3_DIRS)
        list(REMOVE_DUPLICATES _GLFW3_DIRS)
    endif()

    set(${_OUT} ${_GLFW3_DIRS} PARENT_SCOPE)
endfunction()

macro(_GLFW3_SET_FROM_TARGET _TARGET)
    get_target_property(_GLFW3_TARGET_INCLUDE_DIRS ${_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    if(_GLFW3_TARGET_INCLUDE_DIRS AND NOT "${_GLFW3_TARGET_INCLUDE_DIRS}" STREQUAL "_GLFW3_TARGET_INCLUDE_DIRS-NOTFOUND")
        list(GET _GLFW3_TARGET_INCLUDE_DIRS 0 GLFW3_INCLUDE_DIR)
        set( GLFW3_LIBRARY ${_TARGET} )
        set( GLFW3_LIBRARIES ${_TARGET} )
        set( GLFW3_TARGET ${_TARGET} )
        set( GLFW3_FOUND TRUE )
        message("find glfw3: ${GLFW3_INCLUDE_DIR}")
    endif()
endmacro()

_GLFW3_COLLECT_VCPKG_INSTALLED_DIRS(_GLFW3_VCPKG_INSTALLED_DIRS)
foreach(_GLFW3_VCPKG_INSTALLED_DIR ${_GLFW3_VCPKG_INSTALLED_DIRS})
    if(NOT GLFW3_FOUND AND EXISTS "${_GLFW3_VCPKG_INSTALLED_DIR}/share/glfw3/glfw3Config.cmake")
        set(_GLFW3_OLD_CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}")
        set(CMAKE_PREFIX_PATH "${_GLFW3_VCPKG_INSTALLED_DIR};${CMAKE_PREFIX_PATH}")
        find_package(glfw3 CONFIG QUIET PATHS "${_GLFW3_VCPKG_INSTALLED_DIR}" NO_DEFAULT_PATH)
        set(CMAKE_PREFIX_PATH "${_GLFW3_OLD_CMAKE_PREFIX_PATH}")

        if(TARGET glfw)
            _GLFW3_SET_FROM_TARGET(glfw)
        endif()
    endif()
endforeach()

if(NOT GLFW3_FOUND)
    find_package(glfw3 CONFIG QUIET)
    if(TARGET glfw)
        _GLFW3_SET_FROM_TARGET(glfw)
    endif()
endif()

if(NOT GLFW3_FOUND)
    if(WIN32)

        message("Is Windows")
        set(GLFW3_PATH $ENV{GLFW3_PATH})
        if( GLFW3_PATH )

            message("Find GLFW3_PATH env!")
            message(${GLFW3_PATH})

            find_path( GLFW3_INCLUDE_DIR GLFW "${GLFW3_PATH}/include" )
            find_library( GLFW3_LIBRARY glfw3.lib "${GLFW3_PATH}/lib" "${GLFW3_PATH}/lib-vc2019" "${GLFW3_PATH}/lib-vc2015" "${GLFW3_PATH}/build/src/Release")
            if (NOT GLFW3_LIBRARY)
                find_library( GLFW3_LIBRARY glfw3dll.lib "${GLFW3_PATH}/lib")
            endif()

            if( GLFW3_INCLUDE_DIR AND GLFW3_LIBRARY)

                set( GLFW3_FOUND TRUE )

            else()

                set( GLFW3_FOUND FALSE )

            endif()

        else()

            set( GLFW3_FOUND FALSE )
            message("Not Find GLFW3_PATH env!")

        endif()

    else()

        message("Not Windows!")
        find_path( GLFW3_INCLUDE_DIR GLFW "/usr/include" )
        find_library( GLFW3_LIBRARY glfw "/usr/lib/x86_64-linux-gnu/")

        if( GLFW3_INCLUDE_DIR AND GLFW3_LIBRARY)

            message("find glfw3 ${GLFW3_LIBRARY}")
            set( GLFW3_FOUND TRUE )

        else()

        set(GLFW3_PATH $ENV{GLFW3_PATH})
        if( GLFW3_PATH )

            message("Find GLFW3_PATH env!")
            message(${GLFW3_PATH})

            find_path( GLFW3_INCLUDE_DIR GLFW "${GLFW3_PATH}/include" )
            find_library( GLFW3_LIBRARY libglfw.3.dylib "${GLFW3_PATH}/lib" "${GLFW3_PATH}/lib-vc2015")

            if( GLFW3_INCLUDE_DIR AND GLFW3_LIBRARY)

                set( GLFW3_FOUND TRUE )

            else()

                set( GLFW3_FOUND FALSE )

            endif()

        else()

            set( GLFW3_FOUND FALSE )
            message("Not Find GLFW3_PATH env!")

        endif()

        endif()

    endif()
endif()

message("................................................................")
