# Converts the png file ${IN_FILE} to the RGBA8888 file ${OUT_FILE}
# The gAMA chunk is ignored.
# The "-intensity Average" setting prevents ImageMagick from color-converting grayscale files.
# For invalid files, a zero-byte output file is created.
execute_process(
    COMMAND convert -define png:exclude-chunk=gAMA ${IN_FILE} ${CMAKE_CURRENT_LIST_DIR}/temp.png
    ERROR_QUIET
)
if (EXISTS ${CMAKE_CURRENT_LIST_DIR}/temp.png)
    execute_process(COMMAND convert -depth 8 -intensity Average ${CMAKE_CURRENT_LIST_DIR}/temp.png ${OUT_FILE})
    file(REMOVE ${CMAKE_CURRENT_LIST_DIR}/temp.png)
else()
    file(WRITE ${OUT_FILE} "")
endif()
