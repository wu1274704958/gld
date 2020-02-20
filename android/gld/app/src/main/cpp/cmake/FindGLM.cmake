message( "finding GLM!"  )

if(WIN32)

    message("Is Windows")
    set(GLM_PATH $ENV{GLM_PATH})
    if( GLM_PATH )

        message("Find GLM_PATH env!")
        message(${GLM_PATH})

        find_path( GLM_INCLUDE_DIR glm "${GLM_PATH}" )

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

        set( GLM_FOUND FALSE )

    endif()

endif()

message("................................................................")