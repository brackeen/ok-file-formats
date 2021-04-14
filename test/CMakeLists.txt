cmake_minimum_required(VERSION 3.3)

project(OK_FILE_FORMATS_TESTS C)

option(TREAT_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(FUZZ_WITH_ASAN "Use Address Sanitizer when building the fuzz test (Warning: slow)" OFF)

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

# Fuzzing (Compiling for American Fuzzy Lop)
if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    find_program(FUZZING_COMPILER afl-clang)
elseif (CMAKE_C_COMPILER_ID MATCHES "GNU")
    find_program(FUZZING_COMPILER afl-gcc)
endif()

# ok-file-formats
include_directories(..)
file(GLOB src_files "../*.h" "../*.c")
source_group("" FILES ${src_files})

# Generated files
file(GLOB test_png_files "PngSuite/*.png")
file(GLOB test_jpg_files "jpg/*.jpg")

add_custom_target(create_gen_directory ALL COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen)
set(GEN_FILES create_gen_directory)

# Copy small files for fuzzing
if (FUZZING_COMPILER)
    add_custom_target(create_fuzzing_results_directory ALL COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen/fuzzing/afl_results)
    list(APPEND GEN_FILES create_fuzzing_results_directory)

    set(JPG_FUZZING_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen/fuzzing/input/jpg")
    set(PNG_FUZZING_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen/fuzzing/input/png")

    add_custom_target(create_jpg_fuzzing_directory ALL COMMAND ${CMAKE_COMMAND} -E make_directory ${JPG_FUZZING_OUT_DIR})
    add_custom_target(create_png_fuzzing_directory ALL COMMAND ${CMAKE_COMMAND} -E make_directory ${PNG_FUZZING_OUT_DIR})
    list(APPEND GEN_FILES create_jpg_fuzzing_directory)
    list(APPEND GEN_FILES create_png_fuzzing_directory)
endif()

find_program(IMAGEMAGICK_COMMAND magick)
if (NOT IMAGEMAGICK_COMMAND)
    message(FATAL_ERROR "ImageMagick 7 not found")
endif()

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

    if (FUZZING_COMPILER)
        add_custom_command(
            OUTPUT ${PNG_FUZZING_OUT_DIR}/${file_name}.png
            DEPENDS ${png_file}
            COMMAND ${CMAKE_COMMAND} -E copy ${png_file} ${PNG_FUZZING_OUT_DIR}/${file_name}.png
            COMMENT "Converting ${file_name}.png [for fuzzing]"
            VERBATIM
        )
        list(APPEND GEN_FILES ${PNG_FUZZING_OUT_DIR}/${file_name}.png)
    endif()
endforeach()

# Convert jpg files to raw RGBA8888 format, applying exif orientation tags if found.
# Tested against jpeg 8d via ImageMagick.
set(FUZZING_JPG_LIST "jpg-gray" "jpeg444" "LEVEL76" "65500w" "park" "orientation_1" "jpg-size-1x1" "jpg-size-7x7" "jpg-size-33x33" "2004" "gort")
foreach(jpg_file ${test_jpg_files})
    get_filename_component(file_name ${jpg_file} NAME_WE)
    set(gen_file "${CMAKE_CURRENT_BINARY_DIR}/gen/${file_name}.rgba")
    add_custom_command(
        OUTPUT ${gen_file}
        DEPENDS ${jpg_file}
        COMMAND magick ${jpg_file} -auto-orient ${gen_file}
        COMMENT "Converting ${file_name}.jpg"
        VERBATIM
    )
    list(APPEND GEN_FILES ${gen_file})

    if (FUZZING_COMPILER)
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
    endif()
endforeach()

# Generate WAV files
include(GenerateWAVs)

add_custom_target(gen ALL DEPENDS ${GEN_FILES})

# Test
file(GLOB test_src_files "*.h" "*.c")
add_executable(ok-file-formats-test ${src_files} ${test_src_files})
target_compile_options(ok-file-formats-test PUBLIC -DOK_PNG_WRITE_CHUNK_MAX_LENGTH=0x7ffff)
add_dependencies(ok-file-formats-test gen)
if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    # Enable -Wwrite-strings because -Weverything doesn't enable it in all versions of Clang
    set_target_properties(ok-file-formats-test PROPERTIES COMPILE_FLAGS "-fsanitize=address -fno-omit-frame-pointer -O3 -g -Weverything -Wwrite-strings -Wno-padded -Wno-covered-switch-default ${TREAT_WARNINGS_AS_ERRORS_FLAG}")
    set_target_properties(ok-file-formats-test PROPERTIES LINK_FLAGS "-fsanitize=address")
elseif (CMAKE_C_COMPILER_ID MATCHES "GNU")
    set_target_properties(ok-file-formats-test PROPERTIES COMPILE_FLAGS "-fsanitize=address -fno-omit-frame-pointer -O3 -g -Wall -Wextra -Wwrite-strings ${TREAT_WARNINGS_AS_ERRORS_FLAG}")
    set_target_properties(ok-file-formats-test PROPERTIES LINK_FLAGS "-fsanitize=address")
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

# Fuzzing test
if (FUZZING_COMPILER)
    add_executable(ok-file-formats-fuzzing ${src_files} "main_fuzzing.c")
    add_dependencies(ok-file-formats-fuzzing gen)
    if (FUZZ_WITH_ASAN)
        set_target_properties(ok-file-formats-fuzzing PROPERTIES COMPILE_FLAGS "-fsanitize=address -fno-omit-frame-pointer -O3")
        set_target_properties(ok-file-formats-fuzzing PROPERTIES LINK_FLAGS "-fsanitize=address")
    endif()
    set_target_properties(ok-file-formats-fuzzing PROPERTIES C_COMPILER_LAUNCHER ${FUZZING_COMPILER})
endif()
