message( "finding GLM!"  )

set( GLM_FOUND FALSE )
set( GLM_TARGET "" )
set( GLM_LIBRARIES "" )

macro(_GLM_SET_FROM_TARGET)
    unset(_GLM_TARGET_INCLUDE_DIRS)

    if(TARGET glm::glm-header-only)
        get_target_property(_GLM_TARGET_INCLUDE_DIRS glm::glm-header-only INTERFACE_INCLUDE_DIRECTORIES)
    elseif(TARGET glm::glm)
        get_target_property(_GLM_TARGET_INCLUDE_DIRS glm::glm INTERFACE_INCLUDE_DIRECTORIES)
    endif()

    if(_GLM_TARGET_INCLUDE_DIRS AND NOT "${_GLM_TARGET_INCLUDE_DIRS}" STREQUAL "_GLM_TARGET_INCLUDE_DIRS-NOTFOUND")
        list(GET _GLM_TARGET_INCLUDE_DIRS 0 GLM_INCLUDE_DIR)
        set( GLM_FOUND TRUE )
        set( GLM_TARGET glm::glm )
        set( GLM_LIBRARIES glm::glm )
        message("find glm: ${GLM_INCLUDE_DIR}")
    endif()
endmacro()

function(_GLM_APPEND_VCPKG_ROOT _ROOT)
    file(TO_CMAKE_PATH "${_ROOT}" _GLM_ROOT)
    if(NOT "${_GLM_ROOT}" STREQUAL "" AND EXISTS "${_GLM_ROOT}/installed")
        set(_GLM_VCPKG_ROOTS ${_GLM_VCPKG_ROOTS} "${_GLM_ROOT}" PARENT_SCOPE)
    endif()
endfunction()

if(NOT GLM_FOUND)
    set(_GLM_VCPKG_ROOTS)

    if(DEFINED VCPKG_ROOT)
        _GLM_APPEND_VCPKG_ROOT("${VCPKG_ROOT}")
    endif()

    if(DEFINED ENV{VCPKG_ROOT})
        _GLM_APPEND_VCPKG_ROOT("$ENV{VCPKG_ROOT}")
    endif()

    if(DEFINED CMAKE_TOOLCHAIN_FILE)
        get_filename_component(_GLM_TOOLCHAIN_DIR "${CMAKE_TOOLCHAIN_FILE}" PATH)
        get_filename_component(_GLM_SCRIPTS_DIR "${_GLM_TOOLCHAIN_DIR}" PATH)
        get_filename_component(_GLM_ROOT_FROM_TOOLCHAIN "${_GLM_SCRIPTS_DIR}" PATH)
        _GLM_APPEND_VCPKG_ROOT("${_GLM_ROOT_FROM_TOOLCHAIN}")
    endif()

    _GLM_APPEND_VCPKG_ROOT("F:/library/vcpkg")

    if(_GLM_VCPKG_ROOTS)
        list(REMOVE_DUPLICATES _GLM_VCPKG_ROOTS)
    endif()

    set(_GLM_VCPKG_TRIPLETS)
    if(DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND _GLM_VCPKG_TRIPLETS "${VCPKG_TARGET_TRIPLET}")
    endif()

    if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET})
        list(APPEND _GLM_VCPKG_TRIPLETS "$ENV{VCPKG_DEFAULT_TRIPLET}")
    endif()

    if(WIN32)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            list(APPEND _GLM_VCPKG_TRIPLETS x64-windows)
        else()
            list(APPEND _GLM_VCPKG_TRIPLETS x86-windows)
        endif()
    endif()

    foreach(_GLM_VCPKG_ROOT ${_GLM_VCPKG_ROOTS})
        file(GLOB _GLM_INSTALLED_TRIPLETS RELATIVE "${_GLM_VCPKG_ROOT}/installed" "${_GLM_VCPKG_ROOT}/installed/*")
        list(APPEND _GLM_VCPKG_TRIPLETS ${_GLM_INSTALLED_TRIPLETS})
    endforeach()

    if(_GLM_VCPKG_TRIPLETS)
        list(REMOVE_DUPLICATES _GLM_VCPKG_TRIPLETS)
    endif()

    foreach(_GLM_VCPKG_ROOT ${_GLM_VCPKG_ROOTS})
        foreach(_GLM_VCPKG_TRIPLET ${_GLM_VCPKG_TRIPLETS})
            if(EXISTS "${_GLM_VCPKG_ROOT}/installed/${_GLM_VCPKG_TRIPLET}/share/glm/glmConfig.cmake")
                find_package(glm CONFIG QUIET PATHS "${_GLM_VCPKG_ROOT}/installed/${_GLM_VCPKG_TRIPLET}" NO_DEFAULT_PATH)
                if(glm_FOUND)
                    _GLM_SET_FROM_TARGET()
                endif()
            endif()

            if(GLM_FOUND)
                break()
            endif()
        endforeach()

        if(GLM_FOUND)
            break()
        endif()
    endforeach()
endif()

if(NOT GLM_FOUND)
    find_package(glm CONFIG QUIET)
    if(glm_FOUND)
        _GLM_SET_FROM_TARGET()
    endif()
endif()

if(NOT GLM_FOUND)
    if(WIN32)

        message("Is Windows")
        set(GLM_PATH $ENV{GLM_PATH})
        if( GLM_PATH )

            message("Find GLM_PATH env!")
            message(${GLM_PATH})

            find_path( GLM_INCLUDE_DIR glm "${GLM_PATH}" "${GLM_PATH}/include" )

            if( GLM_INCLUDE_DIR )

                set( GLM_FOUND TRUE )

            else()

                set( GLM_FOUND FALSE )

            endif()

        else()

            set( GLM_FOUND FALSE )
            message("Not Find GLM_PATH env!")

        endif()

    else()

        message("Not Windows!")
        find_path( GLM_INCLUDE_DIR glm "/usr/include" )

        if( GLM_INCLUDE_DIR )

            message("find glm!")
            set( GLM_FOUND TRUE )

        else()

        set(GLM_PATH $ENV{GLM_PATH})
        if( GLM_PATH )

            message("Find GLM_PATH env!")
            message(${GLM_PATH})

            find_path( GLM_INCLUDE_DIR glm "${GLM_PATH}" "${GLM_PATH}/include" )

            if( GLM_INCLUDE_DIR )

                set( GLM_FOUND TRUE )

            else()

                set( GLM_FOUND FALSE )

            endif()

        else()

            set( GLM_FOUND FALSE )
            message("Not Find GLM_PATH env!")

        endif()

        endif()

    endif()
endif()

message("................................................................")
