message( "finding FREE_TYPE!"  )

if(WIN32)

    message("Is Windows")
    set(FREE_TYPE_PATH $ENV{FREE_TYPE_PATH})
    if( FREE_TYPE_PATH )

        message("Find FREE_TYPE_PATH env!")
        message(${FREE_TYPE_PATH})

        find_path( FREE_TYPE_INCLUDE_DIR NAMES freetype ft2build.h PATHS "${FREE_TYPE_PATH}/include" )
        find_library( FREE_TYPE_LIBRARY freetype.lib "${FREE_TYPE_PATH}/objs/x64/Release" "${FREE_TYPE_PATH}/win64")

        if( FREE_TYPE_INCLUDE_DIR AND FREE_TYPE_LIBRARY)

            set( FREE_TYPE_FOUND TRUE )

        else()

            set( FREE_TYPE_FOUND FALSE )

        endif()

    else()

        set( FREE_TYPE_FOUND FALSE )
        message("Not Find FREE_TYPE_PATH env!")

    endif()

else()

    	message("Not Windows!")
    	find_path( FREE_TYPE_INCLUDE_DIR NAMES freetype ft2build.h PATHS "/data/data/com.termux/files/usr/include/freetype2" )

	find_library( FREE_TYPE_LIBRARY libfreetype.so "/data/data/com.termux/files/usr/lib")

	message(${FREE_TYPE_INCLUDE_DIR})
	message(${FREE_TYPE_LIBRARY})

        if( FREE_TYPE_INCLUDE_DIR AND FREE_TYPE_LIBRARY)

            	set( FREE_TYPE_FOUND TRUE )

        else()

        set(FREE_TYPE_PATH $ENV{FREE_TYPE_PATH})
        if( FREE_TYPE_PATH )
    
            message("Find FREE_TYPE_PATH env!")
            message(${FREE_TYPE_PATH})
    
            find_path( FREE_TYPE_INCLUDE_DIR ft2build.h "${FREE_TYPE_PATH}/include/freetype2" NO_DEFAULT_PATH 
            NO_PACKAGE_ROOT_PATH 
            NO_CMAKE_PATH
            NO_CMAKE_ENVIRONMENT_PATH)
            find_library( FREE_TYPE_LIBRARY libfreetype.6.dylib  "${FREE_TYPE_PATH}/objs/x64/Release" "${FREE_TYPE_PATH}/lib" NO_DEFAULT_PATH)
            message(FREE_TYPE_INCLUDE_DIR ${FREE_TYPE_INCLUDE_DIR})
	        message(FREE_TYPE_LIBRARY ${FREE_TYPE_LIBRARY})
            if( FREE_TYPE_INCLUDE_DIR AND FREE_TYPE_LIBRARY)
    
                set( FREE_TYPE_FOUND TRUE )
    
            else()
    
                set( FREE_TYPE_FOUND FALSE )
    
            endif()
    
        else()
    
            set( FREE_TYPE_FOUND FALSE )
            message("Not Find FREE_TYPE_PATH env!")
    
        endif()

	endif()


endif(WIN32)

message("................................................................")
