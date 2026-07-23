cmake_minimum_required(VERSION 3.20)
project(aoe2de_export_sld LANGUAGES CXX)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module NumPy)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX') or '.pyd')"
    OUTPUT_VARIABLE PY_EXT_SUFFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(SLD_PYX "${CMAKE_CURRENT_SOURCE_DIR}/sld/sld.pyx")
set(SLD_CPP "${CMAKE_CURRENT_BINARY_DIR}/sld.cpp")

add_custom_command(
    OUTPUT "${SLD_CPP}"
    COMMAND ${Python3_EXECUTABLE} -m cython -3 --cplus
            -o "${SLD_CPP}"
            "${SLD_PYX}"
    DEPENDS "${SLD_PYX}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Cythonizing local AoE2DE SLD module"
)

add_library(aoe2de_export_sld MODULE "${SLD_CPP}")
target_include_directories(aoe2de_export_sld PRIVATE ${Python3_INCLUDE_DIRS} ${Python3_NumPy_INCLUDE_DIRS})
target_link_libraries(aoe2de_export_sld PRIVATE Python3::Module)
target_compile_features(aoe2de_export_sld PRIVATE cxx_std_17)
if(MSVC)
    # The regular Windows CPython distribution is built with the release CRT
    # and does not ship pythonXY_d.lib.  CMake normally selects /MDd for a
    # Debug target, which defines _DEBUG and makes Python's pyconfig.h request
    # that unavailable debug import library.  Keep Debug's symbols and
    # optimization settings, but match the Python interpreter's /MD runtime.
    set_property(TARGET aoe2de_export_sld PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
    )
    target_compile_options(aoe2de_export_sld PRIVATE /U_DEBUG)
    target_compile_definitions(aoe2de_export_sld PRIVATE NDEBUG)
endif()

set(SLD_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/sld")
set_target_properties(aoe2de_export_sld PROPERTIES
    PREFIX ""
    OUTPUT_NAME "sld"
    SUFFIX "${PY_EXT_SUFFIX}"
    LIBRARY_OUTPUT_DIRECTORY "${SLD_OUTPUT_DIRECTORY}"
    RUNTIME_OUTPUT_DIRECTORY "${SLD_OUTPUT_DIRECTORY}"
)

# Visual Studio is a multi-config generator and otherwise appends Debug/ or
# Release/ to the output directory.  The extension must live directly in the
# sld package so that `from sld.sld import SLD` works for every configuration.
foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(aoe2de_export_sld PROPERTIES
        "LIBRARY_OUTPUT_DIRECTORY_${config_upper}" "${SLD_OUTPUT_DIRECTORY}"
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${SLD_OUTPUT_DIRECTORY}"
    )
endforeach()
