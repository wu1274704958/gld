

message( "finding spy!"  )

set(SPY_PATH $ENV{SPY_PATH})
if( SPY_PATH )

    message("Find SPY_PATH env!")
    message(${SPY_PATH})

    find_path( SPY_INCLUDE_DIR spy.hpp "${SPY_PATH}/include" )

    if( SPY_INCLUDE_DIR )

        set( SPY_FOUND TRUE )

    else()

        set( SPY_FOUND FALSE )

    endif()

else()

    set( SPY_FOUND FALSE )
    message("Not Find SPY_PATH env!")

endif()
message("................................................................")