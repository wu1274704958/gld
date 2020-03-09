# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

set(NEED_ASSIMP ON)
set(ThridLibrary "../../../../../3rd_library")
set(ABThridLibrary "C:/Users/admin/Desktop/go/gld/android/3rd_library")
#set(ABThridLibrary "D:/cpp_project/gld/android/3rd_library")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

find_package(MINI_TEST)
find_package(GLM)

include_directories(
        "include"
        "${CMAKE_ANDROID_NDK}/sources/android"
        ${MINI_TEST_INCLUDE_DIR}
        ${GLM_INCLUDE_DIR}
        "../../../../../../include"
        "../../../../../../"
)

if(NEED_ASSIMP)
    include_directories(${ThridLibrary}/assimp)
endif()

aux_source_directory(../../../../../../src GLD_SOURCE_FILES)

add_library( # Sets the name of the library.
        native-lib

        # Sets the library as a shared library.
        SHARED

        ../../../../../../src/RenderDemo.cpp
        # Provides a relative path to your source file(s).
        ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
        NativeActivity.cpp
        ${GLD_SOURCE_FILES}
        )

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.



add_definitions(-DPF_ANDROID)
add_definitions(-D__glad_h_)

find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

find_library(android-lib android)
find_library(GLESv3-lib GLESv3)
find_library(GLESv2-lib GLESv2)
find_library(EGL-lib EGL)

set(assimp-lib ${ABThridLibrary}/assimp/${ANDROID_ABI}/libassimp.a)
set(android_jniiosystem-lib ${ABThridLibrary}/assimp/${ANDROID_ABI}/libandroid_jniiosystem.a)
set(IrrXML-lib ${ABThridLibrary}/assimp/${ANDROID_ABI}/libIrrXML.a)
set(zlibstatic-lib ${ABThridLibrary}/assimp/${ANDROID_ABI}/libzlibstatic.a)

message("go ${log-lib}  ${ANDROID_ABI}" ${assimp-lib})

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

if(NEED_ASSIMP)
    target_link_libraries( # Specifies the target library.
            native-lib

            # Links the target library to the log library
            # included in the NDK.
            ${log-lib}
            ${android-lib}
            ${GLESv3-lib}
            ${GLESv2-lib}
            ${EGL-lib}

            ${assimp-lib}
            ${android_jniiosystem-lib}
            ${IrrXML-lib}
            ${zlibstatic-lib}
        )
else()
    target_link_libraries( # Specifies the target library.
            native-lib

            # Links the target library to the log library
            # included in the NDK.
            ${log-lib}
            ${android-lib}
            ${GLESv3-lib}
            ${GLESv2-lib}
            ${EGL-lib}

            )
endif()