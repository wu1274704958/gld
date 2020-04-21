

message( "finding ft2pp!"  )

    set(FT2PP_PATH $ENV{FT2PP_PATH})
    if( FT2PP_PATH )

        message("Find FT2PP_PATH env!")
        message(${FT2PP_PATH})

        find_path( FT2PP_INCLUDE_DIR ft2pp.hpp "${FT2PP_PATH}/include" )

        if( FT2PP_INCLUDE_DIR )

            set( FT2PP_FOUND TRUE )

        else()

            set( FT2PP_FOUND FALSE )

        endif()

    else()

        set( FT2PP_FOUND FALSE )
        message("Not Find FT2PP_PATH env!")

    endif()
message("................................................................")