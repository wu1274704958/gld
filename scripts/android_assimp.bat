@echo off

set ASSIMP_PATH=C:\Users\admin\Desktop\go\assimp
set CMAKE_PATH="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set ANDROID_NDK_PATH=C:\Users\admin\AppData\Local\Android\Sdk\ndk-bundle
set ANDROID_CMAKE_PATH=%ASSIMP_PATH%\contrib\android-cmake
set MAKE_PATH=%ANDROID_NDK_PATH%/prebuilt/windows-x86_64/bin/make.exe
::set ABI=arm64-v8a
set ABI=armeabi-v7a

rmdir /s /q build
mkdir build
cd build

%CMAKE_PATH% ..\..\..\ ^
  -G"MinGW Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=%ANDROID_NDK_PATH%/build/cmake/android.toolchain.cmake ^
  -DCMAKE_MAKE_PROGRAM=%ANDROID_NDK_PATH%/prebuilt/windows-x86_64/bin/make.exe ^
  -DANDROID_NDK=%ANDROID_NDK_PATH% ^
  -DANDROID_NATIVE_API_LEVEL=22 ^
  -DASSIMP_ANDROID_JNIIOSYSTEM=ON ^
  -DANDROID_ABI=%ABI% ^
  -DASSIMP_BUILD_ZLIB=ON ^
  -DASSIMP_BUILD_TESTS=OFF ^
  -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=%ASSIMP_PATH%\android_build\%ABI% ^
  -DBUILD_SHARED_LIBS=OFF

%MAKE_PATH% 

cd ..

rmdir /s /q %ABI%

mkdir %ABI%

copy build\lib\* %ABI%\