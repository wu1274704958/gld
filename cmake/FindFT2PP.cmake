

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

        message("Not Find FT2PP_PATH env, trying 3rd_party fallback...")
        find_path( FT2PP_INCLUDE_DIR ft2pp.hpp "${CMAKE_SOURCE_DIR}/3rd_party/ft2pp/include" )

        if( FT2PP_INCLUDE_DIR )

            set( FT2PP_FOUND TRUE )
            message("Found ft2pp in 3rd_party: ${FT2PP_INCLUDE_DIR}")

        else()

            set( FT2PP_FOUND FALSE )
            message("ft2pp not found.")

        endif()

    endif()
message("................................................................")