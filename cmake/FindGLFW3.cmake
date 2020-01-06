message( "finding GLFW3!"  )

if(WIN32)

    message("Is Windows")
    set(GLFW3_PATH $ENV{GLFW3_PATH})
    if( GLFW3_PATH )

        message("Find GLFW3_PATH env!")
        message(${GLFW3_PATH})

        find_path( GLFW3_INCLUDE_DIR GLFW "${GLFW3_PATH}/include" )
        find_library( GLFW3_LIBRARY glfw3.lib "${GLFW3_PATH}/lib" "${GLFW3_PATH}/lib-vc2015")

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

        set( GLFW3_FOUND FALSE )

    endif()

endif()

message("................................................................")