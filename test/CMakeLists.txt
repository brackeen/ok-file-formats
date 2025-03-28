cmake_minimum_required(VERSION 3.20)

option(TREAT_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(FUZZ_TEST "Use afl-cc to build ok-file-formats-fuzz-test" OFF)
option(FUZZ_WITH_ASAN "Use Address Sanitizer when building the fuzz test (Warning: slow)" OFF)

# Dependencies
if (FUZZ_TEST)
    # Fuzzing (Compiling for American Fuzzy Lop++)
    find_program(FUZZING_COMPILER afl-cc)
    if (NOT FUZZING_COMPILER)
        message(FATAL_ERROR "afl-cc not found. Cannot build with FUZZ_TEST=ON")
    endif()
    set(CMAKE_C_COMPILER ${FUZZING_COMPILER})
    set(CMAKE_CXX_COMPILER ${FUZZING_COMPILER})
endif()

find_program(IMAGEMAGICK_COMMAND magick)
if (NOT IMAGEMAGICK_COMMAND)
    message(FATAL_ERROR "ImageMagick 7 not found")
endif()

find_program(SOX_COMMAND sox)
if (NOT SOX_COMMAND)
    message(FATAL_ERROR "SoX not found. See http://sox.sourceforge.net/")
endif()

# Project setup
project(OK_FILE_FORMATS_TESTS C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(FileInformation)

if (TREAT_WARNINGS_AS_ERRORS)
    if (CMAKE_C_COMPILER_ID MATCHES "MSVC")
        set(TREAT_WARNINGS_AS_ERRORS_FLAG "/WX")
    else()
        set(TREAT_WARNINGS_AS_ERRORS_FLAG "-Werror")
    endif()
else()
    set(TREAT_WARNINGS_AS_ERRORS_FLAG "")
endif()

include_directories(..)
file(GLOB src_files "../*.h" "../*.c")
source_group("" FILES ${src_files})

# Generated files
file(GLOB test_png_files "PngSuite/*.png")
file(GLOB test_jpg_files "jpg/*.jpg")

add_custom_target(create_gen_directory ALL COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen)
set(GEN_FILES create_gen_directory)

# Copy small files for fuzzing
if (FUZZ_TEST)
    add_custom_target(create_fuzzing_results_directory ALL COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen/fuzzing/afl_results)
    list(APPEND GEN_FILES create_fuzzing_results_directory)

    set(JPG_FUZZING_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen/fuzzing/input/jpg")
    set(PNG_FUZZING_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen/fuzzing/input/png")

    add_custom_target(create_jpg_fuzzing_directory ALL COMMAND ${CMAKE_COMMAND} -E make_directory ${JPG_FUZZING_OUT_DIR})
    add_custom_target(create_png_fuzzing_directory ALL COMMAND ${CMAKE_COMMAND} -E make_directory ${PNG_FUZZING_OUT_DIR})
    list(APPEND GEN_FILES create_jpg_fuzzing_directory)
    list(APPEND GEN_FILES create_png_fuzzing_directory)
endif()

# Convert png files to raw RGBA8888 format.
# Requires ImageMagick.
foreach(png_file ${test_png_files})
    get_filename_component(file_name ${png_file} NAME_WE)
    if (FUZZ_TEST)
        add_custom_command(
            OUTPUT ${PNG_FUZZING_OUT_DIR}/${file_name}.png
            DEPENDS ${png_file}
            COMMAND ${CMAKE_COMMAND} -E copy ${png_file} ${PNG_FUZZING_OUT_DIR}/${file_name}.png
            COMMENT "Converting ${file_name}.png [for fuzzing]"
            VERBATIM
        )
        list(APPEND GEN_FILES ${PNG_FUZZING_OUT_DIR}/${file_name}.png)
    else()
        set(gen_file "${CMAKE_CURRENT_BINARY_DIR}/gen/${file_name}.rgba")
        add_custom_command(
            OUTPUT ${gen_file}
            DEPENDS ${png_file}
            COMMAND ${CMAKE_COMMAND} -DIMAGEMAGICK_COMMAND=${IMAGEMAGICK_COMMAND} -DIN_FILE=${png_file} -DOUT_FILE=${gen_file} -P ${CMAKE_CURRENT_LIST_DIR}/cmake/ConvertPNG.cmake
            COMMENT "Converting ${file_name}.png"
            VERBATIM
        )
        list(APPEND GEN_FILES ${gen_file})
    endif()
endforeach()

# Convert jpg files to raw RGBA8888 format, applying exif orientation tags if found.
# Tested against jpeg 8d via ImageMagick.
set(FUZZING_JPG_LIST "jpg-gray" "jpeg444" "LEVEL76" "65500w" "park" "orientation_1" "jpg-size-1x1" "jpg-size-7x7" "jpg-size-33x33" "2004" "gort")
foreach(jpg_file ${test_jpg_files})
    get_filename_component(file_name ${jpg_file} NAME_WE)
    if (FUZZ_TEST)
        if (${file_name} IN_LIST FUZZING_JPG_LIST)
            add_custom_command(
                OUTPUT ${JPG_FUZZING_OUT_DIR}/${file_name}.jpg
                DEPENDS ${jpg_file}
                COMMAND ${CMAKE_COMMAND} -E copy ${jpg_file} ${JPG_FUZZING_OUT_DIR}/${file_name}.jpg
                COMMENT "Converting ${file_name}.jpg [for fuzzing]"
                VERBATIM
            )
            list(APPEND GEN_FILES ${JPG_FUZZING_OUT_DIR}/${file_name}.jpg)
        endif()
    else()
        set(gen_file "${CMAKE_CURRENT_BINARY_DIR}/gen/${file_name}.rgba")

        # Disable fancy upsampling because it uses a different upsampling method that ok_jpg.
        # See also OK_JPG_IDCT_UPSAMPLING=0 below.
        add_custom_command(
            OUTPUT ${gen_file}
            DEPENDS ${jpg_file}
            COMMAND ${IMAGEMAGICK_COMMAND} -define jpeg:fancy-upsampling=off ${jpg_file} -auto-orient ${gen_file}
            COMMENT "Converting ${file_name}.jpg"
            VERBATIM
        )
        list(APPEND GEN_FILES ${gen_file})
    endif()
endforeach()

# Generate WAV files
include(GenerateWAVs)
add_custom_target(gen ALL DEPENDS ${GEN_FILES})

if (FUZZ_TEST)
    # Fuzz test
    add_executable(ok-file-formats-fuzz-test ${src_files} "main_fuzzing.c")
    add_dependencies(ok-file-formats-fuzz-test gen)
    if (FUZZ_WITH_ASAN)
        set_target_properties(ok-file-formats-fuzz-test PROPERTIES COMPILE_FLAGS "-fsanitize=address -fno-omit-frame-pointer -O3")
        set_target_properties(ok-file-formats-fuzz-test PROPERTIES LINK_FLAGS "-fsanitize=address")
    endif()
else()
    # Correctness test
    file(GLOB test_src_files "*.h" "*.c")
    list(FILTER test_src_files EXCLUDE REGEX ".*main_fuzzing\\.c$")
    add_executable(ok-file-formats-test ${src_files} ${test_src_files})
    add_dependencies(ok-file-formats-test gen)
    if (CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Enable -Wwrite-strings because -Weverything doesn't enable it in all versions of Clang
        set_target_properties(ok-file-formats-test PROPERTIES COMPILE_FLAGS "-fsanitize=address -fno-omit-frame-pointer -O3 -g -Weverything -Wwrite-strings -Wno-padded -Wno-covered-switch-default -Wno-declaration-after-statement ${TREAT_WARNINGS_AS_ERRORS_FLAG}")
        set_target_properties(ok-file-formats-test PROPERTIES LINK_FLAGS "-fsanitize=address")
    elseif (CMAKE_C_COMPILER_ID MATCHES "GNU")
        set_target_properties(ok-file-formats-test PROPERTIES COMPILE_FLAGS "-fsanitize=address -fno-omit-frame-pointer -O3 -g -Wall -Wextra -Wwrite-strings ${TREAT_WARNINGS_AS_ERRORS_FLAG}")
        set_target_properties(ok-file-formats-test PROPERTIES LINK_FLAGS "-fsanitize=address")
    elseif (CMAKE_C_COMPILER_ID MATCHES "MSVC")
        set_target_properties(ok-file-formats-test PROPERTIES COMPILE_FLAGS "/Wall ${TREAT_WARNINGS_AS_ERRORS_FLAG}")
    endif()

    # Disable ok_jpg's IDCT upsampling because it uses a different upsampling method that libjpeg-turbo.
    # See also "jpeg:fancy-upsampling=off" above.
    target_compile_definitions(ok-file-formats-test PUBLIC OK_JPG_IDCT_UPSAMPLING=0)

    # CTest setup
    enable_testing()
    add_test(NAME ok-file-formats-test
        COMMAND ok-file-formats-test
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    )
endif()
