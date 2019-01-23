cmake_minimum_required(VERSION 3.1.0)

set(OK_FILE_FORMATS_VERSION "1.0")
set(OK_FILE_FORMATS_API_VERSION "1")

file(GLOB OK_FILE_FORMATS_SRC *.c *.h)
add_library(ok-file-formats ${OK_FILE_FORMATS_SRC})
target_include_directories(ok-file-formats PUBLIC .)
set_target_properties(ok-file-formats PROPERTIES 
                      OUTPUT_NAME "okfileformats"
                      VERSION "${OK_FILE_FORMATS_VERSION}"
                      SOVERSION "${OK_FILE_FORMATS_API_VERSION}"
                      C_STANDARD 99)
