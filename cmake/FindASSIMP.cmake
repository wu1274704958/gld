message( "finding ASSIMP!"  )

if(WIN32)

    message("Is Windows")
    set(ASSIMP_PATH $ENV{ASSIMP_PATH})
    if( ASSIMP_PATH )

        message("Find ASSIMP_PATH env!")
        message(${ASSIMP_PATH})

        find_path( ASSIMP_INCLUDE_DIR assimp "${ASSIMP_PATH}/assimp" )
        find_library( ASSIMP_LIBRARY assimp.lib assimp-vc140-mt.lib "${ASSIMP_PATH}/lib")

        if( ASSIMP_INCLUDE_DIR AND ASSIMP_LIBRARY)

            set( ASSIMP_FOUND TRUE )

        else()

			set( ASSIMP_FOUND FALSE )

        endif()

    else(ASSIMP_PATH)

		find_path( ASSIMP_INCLUDE_DIR assimp  "${CMAKE_SOURCE_DIR}/3rd_party/assimp")
        find_library( ASSIMP_LIBRARY assimp.lib assimp-vc140-mt.lib "${CMAKE_SOURCE_DIR}/3rd_party/assimp/lib")

		if( ASSIMP_INCLUDE_DIR AND ASSIMP_LIBRARY)

            set( ASSIMP_FOUND TRUE )

        else()

			set( ASSIMP_FOUND FALSE )

        endif()

        message("Not Find ASSIMP_PATH env!")

    endif(ASSIMP_PATH)

else(WIN32)

    find_path( ASSIMP_INCLUDE_DIR assimp "/usr/include" )
    find_library( ASSIMP_LIBRARY assimp  "/usr/lib/x86_64-linux-gnu/")

    if( ASSIMP_INCLUDE_DIR AND ASSIMP_LIBRARY)

        set( ASSIMP_FOUND TRUE )

    else()

        set( ASSIMP_FOUND FALSE )

    endif()

endif(WIN32)

message("................................................................")