cmake_minimum_required(VERSION 2.8.7)

project(OK_FILE_FORMATS_TESTS C)

option(TREAT_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

if (TREAT_WARNINGS_AS_ERRORS)
    if (CMAKE_C_COMPILER_ID MATCHES "MSVC")
        set(TREAT_WARNINGS_AS_ERRORS_FLAG "/WX")
    else()
        set(TREAT_WARNINGS_AS_ERRORS_FLAG "-Werror")
    endif()
else()
    set(TREAT_WARNINGS_AS_ERRORS_FLAG "")
endif()

# ok-file-formats
include_directories(..)
file(GLOB src_files "../*.h" "../*.c")
source_group("" FILES ${src_files})

# Generated files
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
file(GLOB test_png_files "PngSuite/*.png")
file(GLOB test_jpg_files "jpg/*.jpg")

add_custom_target(create_gen_directory ALL COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen)
set(GEN_FILES create_gen_directory)

find_program(IMAGEMAGICK_COMMAND convert)
if (NOT IMAGEMAGICK_COMMAND)
    message("Warning: ImageMagick not found")
else()
    # Convert png files to raw RGBA8888 format.
    # Requires ImageMagick.
    foreach(png_file ${test_png_files})
        get_filename_component(file_name ${png_file} NAME_WE)
        set(gen_file "${CMAKE_CURRENT_BINARY_DIR}/gen/${file_name}.rgba")
        add_custom_command(
            OUTPUT ${gen_file}
            DEPENDS ${png_file}
            COMMAND ${CMAKE_COMMAND} -DIN_FILE=${png_file} -DOUT_FILE=${gen_file} -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ConvertPNG.cmake
            COMMENT "Converting ${file_name}.png"
            VERBATIM
        )
        list(APPEND GEN_FILES ${gen_file})
    endforeach()

    # Convert jpg files to raw RGBA8888 format, applying exif orientation tags if found.
    # Tested against jpeg 8d via ImageMagick.
    foreach(jpg_file ${test_jpg_files})
        get_filename_component(file_name ${jpg_file} NAME_WE)
        set(gen_file "${CMAKE_CURRENT_BINARY_DIR}/gen/${file_name}.rgba")
        add_custom_command(
            OUTPUT ${gen_file}
            DEPENDS ${jpg_file}
            COMMAND convert -auto-orient ${jpg_file} ${gen_file}
            COMMENT "Converting ${file_name}.jpg"
            VERBATIM
        )
        list(APPEND GEN_FILES ${gen_file})
    endforeach()
endif()

# Generate WAV files
include(GenerateWAVs)

add_custom_target(gen ALL DEPENDS ${GEN_FILES})

# Test
file(GLOB test_src_files "*.h" "*.c")
add_executable(ok-file-formats-test ${src_files} ${test_src_files})
add_dependencies(ok-file-formats-test gen)
if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    # Enable -Wwrite-strings because -Weverything doesn't enable it in all versions of Clang
    # TODO: Remove -Wno-sign-conversion
    set_target_properties(ok-file-formats-test PROPERTIES COMPILE_FLAGS "-Weverything -Wwrite-strings -Wno-padded -Wno-covered-switch-default -Wno-sign-conversion ${TREAT_WARNINGS_AS_ERRORS_FLAG}")
elseif (CMAKE_C_COMPILER_ID MATCHES "GNU")
    set_target_properties(ok-file-formats-test PROPERTIES COMPILE_FLAGS "-Wall -Wextra -Wwrite-strings ${TREAT_WARNINGS_AS_ERRORS_FLAG}")
elseif (CMAKE_C_COMPILER_ID MATCHES "MSVC")
    set_target_properties(ok-file-formats-test PROPERTIES COMPILE_FLAGS "/Wall ${TREAT_WARNINGS_AS_ERRORS_FLAG}")
endif()

# CTest setup, using valgrind if found
enable_testing()
find_program(MEMCHECK_COMMAND valgrind)
set(MEMCHECK_COMMAND_OPTIONS "--error-exitcode=1" "--leak-check=full")
if (NOT MEMCHECK_COMMAND)
    add_test(NAME ok-file-formats-test
        COMMAND ok-file-formats-test
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    )
else()
    add_test(
        NAME ok-file-formats-test-memcheck
        COMMAND ${MEMCHECK_COMMAND} ${MEMCHECK_COMMAND_OPTIONS} ./ok-file-formats-test
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    )
endif()
