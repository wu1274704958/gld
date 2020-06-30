message( "finding BASS24!"  )

if(WIN32)

    message("Is Windows")
    set(BASS24_PATH $ENV{BASS24_PATH})
    if( BASS24_PATH )

        message("Find BASS24_PATH env!")
        message(${BASS24_PATH})

        find_path( BASS24_INCLUDE_DIR NAMES bass.h PATHS "${BASS24_PATH}" )
        find_library( BASS24_LIBRARY bass.lib "${BASS24_PATH}/x64" )
        find_library( BASS24_LIBRARY_FLAC bassflac.lib "${BASS24_PATH}/x64" )

        set(BASS24_LIBRARYS ${BASS24_LIBRARY} ${BASS24_LIBRARY_FLAC})
        message(${BASS24_LIBRARYS})

        if( BASS24_INCLUDE_DIR AND BASS24_LIBRARYS)

            set( BASS24_FOUND TRUE )

        else()

            set( BASS24_FOUND FALSE )

        endif()

    else()

        set( BASS24_FOUND FALSE )
        message("Not Find BASS24_PATH env!")

    endif()

else()

    	message("Not Windows!")
    	


endif(WIN32)

message("................................................................")
