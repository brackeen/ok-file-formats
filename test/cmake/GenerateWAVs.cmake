# This file creates all the needed WAV, CAF and RAW files and appends the targets to GEN_FILES
set(WAV_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(WAV_FUZZING_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen/fuzzing/input/wav")
set(CAF_FUZZING_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen/fuzzing/input/caf")

find_program(AFCONVERT_COMMAND afconvert)

if (FUZZING_COMPILER)
    # For the fuzzer, use short (32 frames) silent files
    add_custom_target(create_wav_fuzzing_directory ALL COMMAND ${CMAKE_COMMAND} -E make_directory ${WAV_FUZZING_OUT_DIR})
    add_custom_target(create_caf_fuzzing_directory ALL COMMAND ${CMAKE_COMMAND} -E make_directory ${CAF_FUZZING_OUT_DIR})
    list(APPEND GEN_FILES create_wav_fuzzing_directory)
    list(APPEND GEN_FILES create_caf_fuzzing_directory)

    set(WAV_FUZZING_IN_FILES "${WAV_OUT_DIR}/temp_silence.wav" "${WAV_OUT_DIR}/temp_silence2.wav")
    add_custom_command(
        OUTPUT ${WAV_OUT_DIR}/temp_silence.wav
        COMMAND ${SOX_COMMAND} -n -r 44100 --bits 16 -c 1 ${WAV_OUT_DIR}/temp_silence.wav trim 0s 32s
        COMMENT "Creating for fuzzing: temp_silence.wav"
        VERBATIM
    )
    add_custom_command(
        OUTPUT ${WAV_OUT_DIR}/temp_silence2.wav
        COMMAND ${SOX_COMMAND} -n -r 44100 --bits 16 -c 2 ${WAV_OUT_DIR}/temp_silence2.wav trim 0s 32s
        COMMENT "Creating for fuzzing: temp_silence2.wav"
        VERBATIM
    )
endif()

set(WAV_IN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/wav/sound.wav" "${CMAKE_CURRENT_SOURCE_DIR}/wav/sound2.wav")
set(WAV_IN_FRAMES "44086" "44085")
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

set(WAV_CHANNELS 1)
foreach(WAV_IN_FILE ${WAV_IN_FILES})
    list(FIND WAV_IN_FILES ${WAV_IN_FILE} WAV_IN_FILE_INDEX)
    if (FUZZING_COMPILER)
        list(GET WAV_FUZZING_IN_FILES ${WAV_IN_FILE_INDEX} WAV_FUZZING_IN_FILE)
    endif()

    if (AFCONVERT_COMMAND)
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

            # CAF for fuzzing
            if (FUZZING_COMPILER)
                add_custom_command(
                    OUTPUT ${CAF_FUZZING_OUT_DIR}/${CAF_FILE_NAME}
                    DEPENDS ${WAV_FUZZING_IN_FILE}
                    COMMAND afconvert -f caff -c ${WAV_CHANNELS} -d ${FORMAT} ${WAV_FUZZING_IN_FILE} ${CAF_FUZZING_OUT_DIR}/${CAF_FILE_NAME}
                    COMMENT "Creating ${CAF_FILE_NAME} [for fuzzing]"
                    VERBATIM
                )
                list(APPEND GEN_FILES ${CAF_FUZZING_OUT_DIR}/${CAF_FILE_NAME})
            endif()

            # RAW
            set(RAW_FILE_NAME "sound-${FORMAT}-${WAV_CHANNELS}ch.raw")
            if (FORMAT MATCHES "I8")
                add_custom_command(
                    OUTPUT ${WAV_OUT_DIR}/${RAW_FILE_NAME}
                    DEPENDS ${WAV_OUT_DIR}/${CAF_FILE_NAME}
                    COMMAND ${SOX_COMMAND} ${WAV_OUT_DIR}/${CAF_FILE_NAME} ${WAV_OUT_DIR}/${RAW_FILE_NAME}
                    COMMENT "Creating ${RAW_FILE_NAME}"
                    VERBATIM
                )
            else()
                add_custom_command(
                    OUTPUT ${WAV_OUT_DIR}/${RAW_FILE_NAME}
                    DEPENDS ${WAV_OUT_DIR}/${CAF_FILE_NAME}
                    COMMAND afconvert -f WAVE -c ${WAV_CHANNELS} -d LEI16 ${WAV_OUT_DIR}/${CAF_FILE_NAME} ${WAV_OUT_DIR}/temp.wav
                    COMMAND ${SOX_COMMAND} ${WAV_OUT_DIR}/temp.wav --endian ${WAV_SYSTEM_ENDIAN} ${WAV_OUT_DIR}/${RAW_FILE_NAME}
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

            # CAF for fuzzing
            if (FUZZING_COMPILER)
                add_custom_command(
                    OUTPUT ${CAF_FUZZING_OUT_DIR}/${CAF_FILE_NAME}
                    DEPENDS ${WAV_FUZZING_IN_FILE}
                    COMMAND afconvert -f caff -c ${WAV_CHANNELS} -d ${FORMAT} ${WAV_FUZZING_IN_FILE} ${CAF_FUZZING_OUT_DIR}/${CAF_FILE_NAME}
                    COMMENT "Creating ${CAF_FILE_NAME} [for fuzzing]"
                    VERBATIM
                )
                list(APPEND GEN_FILES ${CAF_FUZZING_OUT_DIR}/${CAF_FILE_NAME})
            endif()
        endforeach()
    endif()
    foreach(FORMAT ${WAV_WAV_FORMATS})
        list(FIND WAV_WAV_FORMATS ${FORMAT} FORMAT_INDEX)

        # WAV
        list(GET WAV_SOX_WAV_ENCODE_PARAMS ${FORMAT_INDEX} ENCODE_PARAMS)
        string(REPLACE " " ";" ENCODE_PARAMS ${ENCODE_PARAMS})
        set(WAV_FILE_NAME "sound-${FORMAT}-${WAV_CHANNELS}ch.wav")
        add_custom_command(
            OUTPUT ${WAV_OUT_DIR}/${WAV_FILE_NAME}
            DEPENDS ${WAV_IN_FILE}
            COMMAND ${SOX_COMMAND} ${WAV_IN_FILE} ${ENCODE_PARAMS} --channels ${WAV_CHANNELS} ${WAV_OUT_DIR}/${WAV_FILE_NAME}
            COMMENT "Creating ${WAV_FILE_NAME}"
            VERBATIM
        )
        list(APPEND GEN_FILES ${WAV_OUT_DIR}/${WAV_FILE_NAME})

        # WAV for fuzzing
        if (FUZZING_COMPILER)
            add_custom_command(
                OUTPUT ${WAV_FUZZING_OUT_DIR}/${WAV_FILE_NAME}
                DEPENDS ${WAV_FUZZING_IN_FILE}
                COMMAND ${SOX_COMMAND} ${WAV_FUZZING_IN_FILE} ${ENCODE_PARAMS} --channels ${WAV_CHANNELS} ${WAV_FUZZING_OUT_DIR}/${WAV_FILE_NAME}
                COMMENT "Creating ${WAV_FILE_NAME} [for fuzzing]"
                VERBATIM
            )
            list(APPEND GEN_FILES ${WAV_FUZZING_OUT_DIR}/${WAV_FILE_NAME})
        endif()

        # RAW
        set(TRIM_PARAM)
        if (${FORMAT} MATCHES "adpcm")
            # SoX is decoding adpcm files with extra samples at the end; force a trim
            list(GET WAV_IN_FRAMES ${WAV_IN_FILE_INDEX} FRAME_COUNT)
            set(TRIM_PARAM "trim" "0s" "${FRAME_COUNT}s")
        endif()
        list(GET WAV_SOX_WAV_DECODE_PARAMS ${FORMAT_INDEX} DECODE_PARAMS)
        string(REPLACE " " ";" DECODE_PARAMS ${DECODE_PARAMS})
        set(RAW_FILE_NAME "sound-${FORMAT}-${WAV_CHANNELS}ch.raw")
        add_custom_command(
            OUTPUT ${WAV_OUT_DIR}/${RAW_FILE_NAME}
            DEPENDS ${WAV_OUT_DIR}/${WAV_FILE_NAME}
            COMMAND ${SOX_COMMAND} ${WAV_OUT_DIR}/${WAV_FILE_NAME} ${DECODE_PARAMS} --channels ${WAV_CHANNELS} ${WAV_OUT_DIR}/${RAW_FILE_NAME} ${TRIM_PARAM}
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
            COMMAND ${SOX_COMMAND} ${WAV_IN_FILE} ${ENCODE_PARAMS} --channels ${WAV_CHANNELS} ${WAV_OUT_DIR}/${WAV_FILE_NAME}
            COMMENT "Creating ${WAV_FILE_NAME}"
            VERBATIM
        )
        list(APPEND GEN_FILES ${WAV_OUT_DIR}/${WAV_FILE_NAME})

        # WAV for fuzzing
        if (FUZZING_COMPILER)
            add_custom_command(
                OUTPUT ${WAV_FUZZING_OUT_DIR}/${WAV_FILE_NAME}
                DEPENDS ${WAV_FUZZING_IN_FILE}
                COMMAND ${SOX_COMMAND} ${WAV_FUZZING_IN_FILE} ${ENCODE_PARAMS} --channels ${WAV_CHANNELS} ${WAV_FUZZING_OUT_DIR}/${WAV_FILE_NAME}
                COMMENT "Creating ${WAV_FILE_NAME} [for fuzzing]"
                VERBATIM
            )
            list(APPEND GEN_FILES ${WAV_FUZZING_OUT_DIR}/${WAV_FILE_NAME})
        endif()

        # RAW
        list(GET WAV_SOX_COMMON_DECODE_PARAMS ${FORMAT_INDEX} DECODE_PARAMS)
        string(REPLACE " " ";" DECODE_PARAMS ${DECODE_PARAMS})
        set(RAW_FILE_NAME "sound-${FORMAT}-${WAV_CHANNELS}ch.raw")
        add_custom_command(
            OUTPUT ${WAV_OUT_DIR}/${RAW_FILE_NAME}
            DEPENDS ${WAV_OUT_DIR}/${WAV_FILE_NAME}
            COMMAND ${SOX_COMMAND} ${WAV_OUT_DIR}/${WAV_FILE_NAME} ${DECODE_PARAMS} --channels ${WAV_CHANNELS} ${WAV_OUT_DIR}/${RAW_FILE_NAME}
            COMMENT "Creating ${RAW_FILE_NAME}"
            VERBATIM
        )
        list(APPEND GEN_FILES ${WAV_OUT_DIR}/${RAW_FILE_NAME})
    endforeach()
    set(WAV_CHANNELS 2)
endforeach()
