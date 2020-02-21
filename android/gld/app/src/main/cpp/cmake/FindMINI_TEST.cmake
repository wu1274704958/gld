

message( "finding mini-test!"  )

    set(MINI_TEST_PATH $ENV{MINI_TEST_PATH})
    if( MINI_TEST_PATH )

        message("Find MINI_TEST_PATH env!")
        message(${MINI_TEST_PATH})

        string(REPLACE "\\" "/" MINI_TEST_PATH_R "${MINI_TEST_PATH}/include")

        set(CMAKE_FIND_ROOT_PATH ${MINI_TEST_PATH_R})

        message(${MINI_TEST_PATH_R})

        find_path( MINI_TEST_INCLUDE_DIR dbg.hpp HINTS ${MINI_TEST_PATH_R})

        if( MINI_TEST_INCLUDE_DIR )

            set( MINI_TEST_FOUND TRUE )

        else()
            if(MINI_TEST_PATH_R)
                set( MINI_TEST_INCLUDE_DIR ${MINI_TEST_PATH_R})
                set( MINI_TEST_FOUND TRUE )
            else()
                set( MINI_TEST_FOUND FALSE )
            endif()
        endif()

    else()

        set( MINI_TEST_FOUND FALSE )
        message("Not Find MINI_TEST_PATH env!")

    endif()
message("................................................................")