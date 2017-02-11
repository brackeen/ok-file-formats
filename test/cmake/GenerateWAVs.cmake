set(WAV_IN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/wav/sound.wav" "${CMAKE_CURRENT_SOURCE_DIR}/wav/sound2.wav")
set(WAV_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")

set(WAV_SYSTEM_ENDIAN "little")

set(WAV_COMMON_FORMATS "BEI16" "BEI24" "BEI32" "BEF32" "BEF64" "LEI16" "LEI24" "LEI32" "LEF32" "LEF64")
set(WAV_CAF_FORMATS "I8" "ulaw" "alaw" "ima4")
set(WAV_WAV_FORMATS "UI8" "ulaw-wav" "alaw-wav" "ima-adpcm" "ms-adpcm")

set(WAV_SOX_WAV_ENCODE_PARAMS
    "--encoding unsigned-integer --bits 8"
    "--encoding mu-law --bits 8"
    "--encoding a-law --bits 8"
    "--encoding ima-adpcm"
    "--encoding ms-adpcm"
)
set(WAV_SOX_WAV_DECODE_PARAMS
    "--encoding unsigned-integer --bits 8"
    "--endian ${WAV_SYSTEM_ENDIAN} --encoding signed-integer --bits 16"
    "--endian ${WAV_SYSTEM_ENDIAN} --encoding signed-integer --bits 16"
    "--endian ${WAV_SYSTEM_ENDIAN} --encoding signed-integer --bits 16"
    "--endian ${WAV_SYSTEM_ENDIAN} --encoding signed-integer --bits 16"
)
set(WAV_SOX_COMMON_ENCODE_PARAMS
    "--endian big --encoding signed-integer --bits 16"
    "--endian big --encoding signed-integer --bits 24"
    "--endian big --encoding signed-integer --bits 32"
    "--endian big --encoding floating-point --bits 32"
    "--endian big --encoding floating-point --bits 64"
    "--endian little --encoding signed-integer --bits 16"
    "--endian little --encoding signed-integer --bits 24"
    "--endian little --encoding signed-integer --bits 32"
    "--endian little --encoding floating-point --bits 32"
    "--endian little --encoding floating-point --bits 64"
)
set(WAV_SOX_COMMON_DECODE_PARAMS
    "--endian big"
    "--endian big"
    "--endian big"
    "--endian big"
    "--endian big"
    "--endian little"
    "--endian little"
    "--endian little"
    "--endian little"
    "--endian little"
)

find_program(AFCONVERT_COMMAND afconvert)
find_program(SOX_COMMAND sox)

set(WAV_CHANNELS 1)

foreach(WAV_IN_FILE ${WAV_IN_FILES})
    if (AFCONVERT_COMMAND AND SOX_COMMAND)
        foreach(FORMAT ${WAV_CAF_FORMATS})
            # CAF
            set(CAF_FILE_NAME "sound-${FORMAT}-${WAV_CHANNELS}ch.caf")
            add_custom_command(
                OUTPUT ${WAV_OUT_DIR}/${CAF_FILE_NAME}
                DEPENDS ${WAV_IN_FILE}
                COMMAND afconvert -f caff -c ${WAV_CHANNELS} -d ${FORMAT} ${WAV_IN_FILE} ${WAV_OUT_DIR}/${CAF_FILE_NAME}
                COMMENT "Creating ${CAF_FILE_NAME}"
                VERBATIM
            )
            list(APPEND GEN_FILES ${WAV_OUT_DIR}/${CAF_FILE_NAME})

            # RAW
            set(RAW_FILE_NAME "sound-${FORMAT}-${WAV_CHANNELS}ch.raw")
            if (FORMAT MATCHES "I8")
                add_custom_command(
                    OUTPUT ${WAV_OUT_DIR}/${RAW_FILE_NAME}
                    DEPENDS ${WAV_OUT_DIR}/${CAF_FILE_NAME}
                    COMMAND sox ${WAV_OUT_DIR}/${CAF_FILE_NAME} ${WAV_OUT_DIR}/${RAW_FILE_NAME}
                    COMMENT "Creating ${RAW_FILE_NAME}"
                    VERBATIM
                )
            else()
                add_custom_command(
                    OUTPUT ${WAV_OUT_DIR}/${RAW_FILE_NAME}
                    DEPENDS ${WAV_OUT_DIR}/${CAF_FILE_NAME}
                    COMMAND afconvert -f WAVE -c ${WAV_CHANNELS} -d LEI16 ${WAV_OUT_DIR}/${CAF_FILE_NAME} ${WAV_OUT_DIR}/temp.wav
                    COMMAND sox ${WAV_OUT_DIR}/temp.wav --endian ${WAV_SYSTEM_ENDIAN} ${WAV_OUT_DIR}/${RAW_FILE_NAME}
                    COMMENT "Creating ${RAW_FILE_NAME}"
                    VERBATIM
                )
            endif()
            list(APPEND GEN_FILES ${WAV_OUT_DIR}/${RAW_FILE_NAME})
        endforeach()
        foreach(FORMAT ${WAV_COMMON_FORMATS})
            # CAF
            set(CAF_FILE_NAME "sound-${FORMAT}-${WAV_CHANNELS}ch.caf")
            add_custom_command(
                OUTPUT ${WAV_OUT_DIR}/${CAF_FILE_NAME}
                DEPENDS ${WAV_IN_FILE}
                COMMAND afconvert -f caff -c ${WAV_CHANNELS} -d ${FORMAT} ${WAV_IN_FILE} ${WAV_OUT_DIR}/${CAF_FILE_NAME}
                COMMENT "Creating ${CAF_FILE_NAME}"
                VERBATIM
            )
            list(APPEND GEN_FILES ${WAV_OUT_DIR}/${CAF_FILE_NAME})
        endforeach()
    endif()
    if (SOX_COMMAND)
        foreach(FORMAT ${WAV_WAV_FORMATS})
            list(FIND WAV_WAV_FORMATS ${FORMAT} FORMAT_INDEX)

            # WAV
            list(GET WAV_SOX_WAV_ENCODE_PARAMS ${FORMAT_INDEX} ENCODE_PARAMS)
            string(REPLACE " " ";" ENCODE_PARAMS ${ENCODE_PARAMS})
            set(WAV_FILE_NAME "sound-${FORMAT}-${WAV_CHANNELS}ch.wav")
            add_custom_command(
                OUTPUT ${WAV_OUT_DIR}/${WAV_FILE_NAME}
                DEPENDS ${WAV_IN_FILE}
                COMMAND sox ${WAV_IN_FILE} ${ENCODE_PARAMS} --channels ${WAV_CHANNELS} ${WAV_OUT_DIR}/${WAV_FILE_NAME}
                COMMENT "Creating ${WAV_FILE_NAME}"
                VERBATIM
            )
            list(APPEND GEN_FILES ${WAV_OUT_DIR}/${WAV_FILE_NAME})

            # RAW
            list(GET WAV_SOX_WAV_DECODE_PARAMS ${FORMAT_INDEX} DECODE_PARAMS)
            string(REPLACE " " ";" DECODE_PARAMS ${DECODE_PARAMS})
            set(RAW_FILE_NAME "sound-${FORMAT}-${WAV_CHANNELS}ch.raw")
            add_custom_command(
                OUTPUT ${WAV_OUT_DIR}/${RAW_FILE_NAME}
                DEPENDS ${WAV_OUT_DIR}/${WAV_FILE_NAME}
                COMMAND sox ${WAV_OUT_DIR}/${WAV_FILE_NAME} ${DECODE_PARAMS} --channels ${WAV_CHANNELS} ${WAV_OUT_DIR}/${RAW_FILE_NAME}
                COMMENT "Creating ${RAW_FILE_NAME}"
                VERBATIM
            )
            list(APPEND GEN_FILES ${WAV_OUT_DIR}/${RAW_FILE_NAME})
        endforeach()
        foreach(FORMAT ${WAV_COMMON_FORMATS})
            list(FIND WAV_COMMON_FORMATS ${FORMAT} FORMAT_INDEX)

            # WAV
            list(GET WAV_SOX_COMMON_ENCODE_PARAMS ${FORMAT_INDEX} ENCODE_PARAMS)
            string(REPLACE " " ";" ENCODE_PARAMS ${ENCODE_PARAMS})
            set(WAV_FILE_NAME "sound-${FORMAT}-${WAV_CHANNELS}ch.wav")
            add_custom_command(
                OUTPUT ${WAV_OUT_DIR}/${WAV_FILE_NAME}
                DEPENDS ${WAV_IN_FILE}
                COMMAND sox ${WAV_IN_FILE} ${ENCODE_PARAMS} --channels ${WAV_CHANNELS} ${WAV_OUT_DIR}/${WAV_FILE_NAME}
                COMMENT "Creating ${WAV_FILE_NAME}"
                VERBATIM
            )
            list(APPEND GEN_FILES ${WAV_OUT_DIR}/${WAV_FILE_NAME})

            # RAW
            list(GET WAV_SOX_COMMON_DECODE_PARAMS ${FORMAT_INDEX} DECODE_PARAMS)
            string(REPLACE " " ";" DECODE_PARAMS ${DECODE_PARAMS})
            set(RAW_FILE_NAME "sound-${FORMAT}-${WAV_CHANNELS}ch.raw")
            add_custom_command(
                OUTPUT ${WAV_OUT_DIR}/${RAW_FILE_NAME}
                DEPENDS ${WAV_OUT_DIR}/${WAV_FILE_NAME}
                COMMAND sox ${WAV_OUT_DIR}/${WAV_FILE_NAME} ${DECODE_PARAMS} --channels ${WAV_CHANNELS} ${WAV_OUT_DIR}/${RAW_FILE_NAME}
                COMMENT "Creating ${RAW_FILE_NAME}"
                VERBATIM
            )
            list(APPEND GEN_FILES ${WAV_OUT_DIR}/${RAW_FILE_NAME})
        endforeach()
    endif()
    set(WAV_CHANNELS 2)
endforeach()
