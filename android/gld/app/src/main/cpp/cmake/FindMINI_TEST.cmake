

message( "finding mini-test!"  )

    set(MINI_TEST_PATH $ENV{MINI_TEST_PATH})
    if( MINI_TEST_PATH )

        message("Find MINI_TEST_PATH env!")
        message(${MINI_TEST_PATH})

        find_path( MINI_TEST_INCLUDE_DIR dbg.hpp "${MINI_TEST_PATH}/include" )

        if( MINI_TEST_INCLUDE_DIR )

            set( MINI_TEST_FOUND TRUE )

        else()

            set( MINI_TEST_FOUND FALSE )

        endif()

    else()

        set( MINI_TEST_FOUND FALSE )
        message("Not Find MINI_TEST_PATH env!")

    endif()
message("................................................................")