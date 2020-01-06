message( "finding GLFW3!"  )

if(WIN32)

    message("Is Windows")
    set(SOIL_PATH $ENV{SOIL_PATH})
    if( SOIL_PATH )

        message("Find SOIL_PATH env!")
        message(${SOIL_PATH})

        find_path( SOIL_INCLUDE_DIR SOIL.h "${SOIL_PATH}/src" )
        find_library( SOIL_LIBRARY libSOIL.lib "${SOIL_PATH}/lib")

        if( SOIL_INCLUDE_DIR AND SOIL_LIBRARY)

            set( SOIL_FOUND TRUE )

        else()

            set( SOIL_FOUND FALSE )

        endif()

    else()

        set( SOIL_FOUND FALSE )
        message("Not Find SOIL_PATH env!")

    endif()

else()

    message("Not Windows!")

endif()

message("................................................................")