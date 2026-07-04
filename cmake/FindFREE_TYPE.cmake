message( "finding FREE_TYPE!"  )

set( FREE_TYPE_FOUND FALSE )
set( FREE_TYPE_TARGET "" )
set( FREE_TYPE_LIBRARIES "" )

function(_FREE_TYPE_COLLECT_VCPKG_INSTALLED_DIRS _OUT)
    set(_FREE_TYPE_ROOTS)

    if(DEFINED VCPKG_ROOT)
        list(APPEND _FREE_TYPE_ROOTS "${VCPKG_ROOT}")
    endif()

    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND _FREE_TYPE_ROOTS "$ENV{VCPKG_ROOT}")
    endif()

    if(DEFINED CMAKE_TOOLCHAIN_FILE)
        get_filename_component(_FREE_TYPE_TOOLCHAIN_DIR "${CMAKE_TOOLCHAIN_FILE}" PATH)
        get_filename_component(_FREE_TYPE_SCRIPTS_DIR "${_FREE_TYPE_TOOLCHAIN_DIR}" PATH)
        get_filename_component(_FREE_TYPE_ROOT_FROM_TOOLCHAIN "${_FREE_TYPE_SCRIPTS_DIR}" PATH)
        list(APPEND _FREE_TYPE_ROOTS "${_FREE_TYPE_ROOT_FROM_TOOLCHAIN}")
    endif()

    list(APPEND _FREE_TYPE_ROOTS "F:/library/vcpkg")

    set(_FREE_TYPE_DIRS)
    foreach(_FREE_TYPE_ROOT ${_FREE_TYPE_ROOTS})
        file(TO_CMAKE_PATH "${_FREE_TYPE_ROOT}" _FREE_TYPE_ROOT)
        if(EXISTS "${_FREE_TYPE_ROOT}/installed")
            set(_FREE_TYPE_TRIPLETS)

            if(DEFINED VCPKG_TARGET_TRIPLET)
                list(APPEND _FREE_TYPE_TRIPLETS "${VCPKG_TARGET_TRIPLET}")
            endif()

            if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET})
                list(APPEND _FREE_TYPE_TRIPLETS "$ENV{VCPKG_DEFAULT_TRIPLET}")
            endif()

            if(WIN32)
                if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                    list(APPEND _FREE_TYPE_TRIPLETS x64-windows)
                else()
                    list(APPEND _FREE_TYPE_TRIPLETS x86-windows)
                endif()
            endif()

            file(GLOB _FREE_TYPE_INSTALLED_TRIPLETS RELATIVE "${_FREE_TYPE_ROOT}/installed" "${_FREE_TYPE_ROOT}/installed/*")
            list(APPEND _FREE_TYPE_TRIPLETS ${_FREE_TYPE_INSTALLED_TRIPLETS})

            if(_FREE_TYPE_TRIPLETS)
                list(REMOVE_DUPLICATES _FREE_TYPE_TRIPLETS)
            endif()

            foreach(_FREE_TYPE_TRIPLET ${_FREE_TYPE_TRIPLETS})
                if(EXISTS "${_FREE_TYPE_ROOT}/installed/${_FREE_TYPE_TRIPLET}")
                    list(APPEND _FREE_TYPE_DIRS "${_FREE_TYPE_ROOT}/installed/${_FREE_TYPE_TRIPLET}")
                endif()
            endforeach()
        endif()
    endforeach()

    if(_FREE_TYPE_DIRS)
        list(REMOVE_DUPLICATES _FREE_TYPE_DIRS)
    endif()

    set(${_OUT} ${_FREE_TYPE_DIRS} PARENT_SCOPE)
endfunction()

macro(_FREE_TYPE_SET_FROM_TARGET _TARGET)
    unset(_FREE_TYPE_TARGET_INCLUDE_DIRS)

    if(TARGET freetype)
        get_target_property(_FREE_TYPE_TARGET_INCLUDE_DIRS freetype INTERFACE_INCLUDE_DIRECTORIES)
    endif()

    if(NOT _FREE_TYPE_TARGET_INCLUDE_DIRS OR "${_FREE_TYPE_TARGET_INCLUDE_DIRS}" STREQUAL "_FREE_TYPE_TARGET_INCLUDE_DIRS-NOTFOUND")
        get_target_property(_FREE_TYPE_TARGET_INCLUDE_DIRS ${_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    endif()

    if(_FREE_TYPE_TARGET_INCLUDE_DIRS AND NOT "${_FREE_TYPE_TARGET_INCLUDE_DIRS}" STREQUAL "_FREE_TYPE_TARGET_INCLUDE_DIRS-NOTFOUND")
        list(GET _FREE_TYPE_TARGET_INCLUDE_DIRS 0 FREE_TYPE_INCLUDE_DIR)
        set( FREE_TYPE_LIBRARY ${_TARGET} )
        set( FREE_TYPE_LIBRARIES ${_TARGET} )
        set( FREE_TYPE_TARGET ${_TARGET} )
        set( FREE_TYPE_FOUND TRUE )
        message("find freetype: ${FREE_TYPE_INCLUDE_DIR}")
    endif()
endmacro()

_FREE_TYPE_COLLECT_VCPKG_INSTALLED_DIRS(_FREE_TYPE_VCPKG_INSTALLED_DIRS)
foreach(_FREE_TYPE_VCPKG_INSTALLED_DIR ${_FREE_TYPE_VCPKG_INSTALLED_DIRS})
    if(NOT FREE_TYPE_FOUND AND EXISTS "${_FREE_TYPE_VCPKG_INSTALLED_DIR}/share/freetype/freetype-config.cmake")
        set(_FREE_TYPE_OLD_CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}")
        set(CMAKE_PREFIX_PATH "${_FREE_TYPE_VCPKG_INSTALLED_DIR};${CMAKE_PREFIX_PATH}")
        find_package(freetype CONFIG QUIET PATHS "${_FREE_TYPE_VCPKG_INSTALLED_DIR}" NO_DEFAULT_PATH)
        set(CMAKE_PREFIX_PATH "${_FREE_TYPE_OLD_CMAKE_PREFIX_PATH}")

        if(TARGET Freetype::Freetype)
            _FREE_TYPE_SET_FROM_TARGET(Freetype::Freetype)
        elseif(TARGET freetype)
            _FREE_TYPE_SET_FROM_TARGET(freetype)
        endif()
    endif()
endforeach()

if(NOT FREE_TYPE_FOUND)
    find_package(freetype CONFIG QUIET)
    if(TARGET Freetype::Freetype)
        _FREE_TYPE_SET_FROM_TARGET(Freetype::Freetype)
    elseif(TARGET freetype)
        _FREE_TYPE_SET_FROM_TARGET(freetype)
    endif()
endif()

if(NOT FREE_TYPE_FOUND)
    if(WIN32)

        message("Is Windows")
        set(FREE_TYPE_PATH $ENV{FREE_TYPE_PATH})
        if( FREE_TYPE_PATH )

            message("Find FREE_TYPE_PATH env!")
            message(${FREE_TYPE_PATH})

            find_path( FREE_TYPE_INCLUDE_DIR NAMES freetype ft2build.h PATHS "${FREE_TYPE_PATH}/include" )
            find_library( FREE_TYPE_LIBRARY freetype.lib "${FREE_TYPE_PATH}/lib" "${FREE_TYPE_PATH}/objs/x64/Release" "${FREE_TYPE_PATH}/win64" "${FREE_TYPE_PATH}/build/Release")

            if( FREE_TYPE_INCLUDE_DIR AND FREE_TYPE_LIBRARY)

                set( FREE_TYPE_FOUND TRUE )

            else()

                set( FREE_TYPE_FOUND FALSE )

            endif()

        else()

            set( FREE_TYPE_FOUND FALSE )
            message("Not Find FREE_TYPE_PATH env!")

        endif()

    else()

        message("Not Windows!")
        find_path( FREE_TYPE_INCLUDE_DIR NAMES freetype ft2build.h PATHS "/data/data/com.termux/files/usr/include/freetype2" )

        find_library( FREE_TYPE_LIBRARY libfreetype.so "/data/data/com.termux/files/usr/lib")

        message(${FREE_TYPE_INCLUDE_DIR})
        message(${FREE_TYPE_LIBRARY})

        if( FREE_TYPE_INCLUDE_DIR AND FREE_TYPE_LIBRARY)

            set( FREE_TYPE_FOUND TRUE )

        else()

        set(FREE_TYPE_PATH $ENV{FREE_TYPE_PATH})
        if( FREE_TYPE_PATH )

            message("Find FREE_TYPE_PATH env!")
            message(${FREE_TYPE_PATH})

            find_path( FREE_TYPE_INCLUDE_DIR ft2build.h "${FREE_TYPE_PATH}/include/freetype2" NO_DEFAULT_PATH
            NO_PACKAGE_ROOT_PATH
            NO_CMAKE_PATH
            NO_CMAKE_ENVIRONMENT_PATH)
            find_library( FREE_TYPE_LIBRARY libfreetype.6.dylib  "${FREE_TYPE_PATH}/objs/x64/Release" "${FREE_TYPE_PATH}/lib" NO_DEFAULT_PATH)
            message(FREE_TYPE_INCLUDE_DIR ${FREE_TYPE_INCLUDE_DIR})
            message(FREE_TYPE_LIBRARY ${FREE_TYPE_LIBRARY})
            if( FREE_TYPE_INCLUDE_DIR AND FREE_TYPE_LIBRARY)

                set( FREE_TYPE_FOUND TRUE )

            else()

                set( FREE_TYPE_FOUND FALSE )

            endif()

        else()

            set( FREE_TYPE_FOUND FALSE )
            message("Not Find FREE_TYPE_PATH env!")

        endif()

        endif()

    endif()
endif()

message("................................................................")
