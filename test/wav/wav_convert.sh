#!/bin/bash
#
# This script is meant to be run from the parent directory.
#
# Requres SoX
#
# brew install sox --with-libsndfile
#

# Mono sound with even number of frames, Stereo sound with odd number of frames
IN_WAVS=( wav/sound.wav wav/sound2.wav )
IN_FRAMES=( 44086 44085 )
OUT=gen

mkdir -p $OUT

caf_data_formats=( I8 ulaw alaw ima4 BEI16 BEI24 BEI32 BEF32 BEF64 LEI16 LEI24 LEI32 LEF32 LEF64 )

# Set this to "big" if you're testing on a big-endian system... but you're not, so don't.
system_endian=little

# TODO:
# WAV: IMA-ADPCM 
# WAV: MS-ADPCM

# CAF files (requires afconvert - SoX doesn't handle all possible CAF formats)
if command -v afconvert >/dev/null 2>&1; then
    echo ${0##*/}: Generating CAF sounds...

    for channels in `seq 1 2`;
    do
        IN_WAV=${IN_WAVS[$channels - 1]}
        for data_format in "${caf_data_formats[@]}"
        do
            afconvert -f caff -c $channels -d $data_format $IN_WAV $OUT/sound-${data_format}-${channels}ch.caf
        done
    
        # CAF-only raw files
        sox $OUT/sound-I8-${channels}ch.caf --channels $channels $OUT/sound-I8-${channels}ch.raw
        sox $OUT/sound-ulaw-${channels}ch.caf --endian $system_endian --encoding signed-integer --bits 16 --channels $channels $OUT/sound-ulaw-${channels}ch.raw
        sox $OUT/sound-alaw-${channels}ch.caf --endian $system_endian --encoding signed-integer --bits 16 --channels $channels $OUT/sound-alaw-${channels}ch.raw
        
        afconvert -f caff -c $channels -d LEI16 $OUT/sound-ima4-${channels}ch.caf $OUT/temp.wav
        sox $OUT/temp.wav --endian $system_endian $OUT/sound-ima4-${channels}ch.raw
        rm $OUT/temp.wav
    done
fi

echo ${0##*/}: Generating WAV sounds...
for channels in `seq 1 2`;
do
    IN_WAV=${IN_WAVS[$channels - 1]}
    
    # 8-bit WAVE files
    # Note that ulaw/alaw encoder for WAVE has slightly different output than the one for CAF, so it needs a seperate RAW file.
    sox $IN_WAV --encoding unsigned-integer --bits 8 --channels $channels $OUT/sound-UI8-${channels}ch.wav
    sox $IN_WAV --encoding mu-law --bits 8 --channels $channels $OUT/sound-ulaw-wav-${channels}ch.wav
    sox $IN_WAV --encoding a-law --bits 8 --channels $channels $OUT/sound-alaw-wav-${channels}ch.wav
    
    # 8-bit RAW files
    sox $OUT/sound-UI8-${channels}ch.wav --channels $channels $OUT/sound-UI8-${channels}ch.raw
    sox $OUT/sound-ulaw-wav-${channels}ch.wav --endian $system_endian --encoding signed-integer --bits 16 --channels $channels $OUT/sound-ulaw-wav-${channels}ch.raw
    sox $OUT/sound-alaw-wav-${channels}ch.wav --endian $system_endian --encoding signed-integer --bits 16 --channels $channels $OUT/sound-alaw-wav-${channels}ch.raw
    
    # Little endian WAVE files
    sox $IN_WAV --endian little --encoding signed-integer --bits 16 --channels $channels $OUT/sound-LEI16-${channels}ch.wav
    sox $IN_WAV --endian little --encoding signed-integer --bits 24 --channels $channels $OUT/sound-LEI24-${channels}ch.wav
    sox $IN_WAV --endian little --encoding signed-integer --bits 32 --channels $channels $OUT/sound-LEI32-${channels}ch.wav
    sox $IN_WAV --endian little --encoding floating-point --bits 32 --channels $channels $OUT/sound-LEF32-${channels}ch.wav
    sox $IN_WAV --endian little --encoding floating-point --bits 64 --channels $channels $OUT/sound-LEF64-${channels}ch.wav
    
    # Little endian RAW files
    sox $OUT/sound-LEI16-${channels}ch.wav --endian little $OUT/sound-LEI16-${channels}ch.raw
    sox $OUT/sound-LEI24-${channels}ch.wav --endian little $OUT/sound-LEI24-${channels}ch.raw
    sox $OUT/sound-LEI32-${channels}ch.wav --endian little $OUT/sound-LEI32-${channels}ch.raw
    sox $OUT/sound-LEF32-${channels}ch.wav --endian little $OUT/sound-LEF32-${channels}ch.raw
    sox $OUT/sound-LEF64-${channels}ch.wav --endian little $OUT/sound-LEF64-${channels}ch.raw  

    # Big endian WAVE files
    sox $IN_WAV --endian big --encoding signed-integer --bits 16 --channels $channels $OUT/sound-BEI16-${channels}ch.wav
    sox $IN_WAV --endian big --encoding signed-integer --bits 24 --channels $channels $OUT/sound-BEI24-${channels}ch.wav
    sox $IN_WAV --endian big --encoding signed-integer --bits 32 --channels $channels $OUT/sound-BEI32-${channels}ch.wav
    sox $IN_WAV --endian big --encoding floating-point --bits 32 --channels $channels $OUT/sound-BEF32-${channels}ch.wav
    sox $IN_WAV --endian big --encoding floating-point --bits 64 --channels $channels $OUT/sound-BEF64-${channels}ch.wav
    
    # Big endian RAW files
    sox $OUT/sound-BEI16-${channels}ch.wav --endian big $OUT/sound-BEI16-${channels}ch.raw
    sox $OUT/sound-BEI24-${channels}ch.wav --endian big $OUT/sound-BEI24-${channels}ch.raw
    sox $OUT/sound-BEI32-${channels}ch.wav --endian big $OUT/sound-BEI32-${channels}ch.raw
    sox $OUT/sound-BEF32-${channels}ch.wav --endian big $OUT/sound-BEF32-${channels}ch.raw
    sox $OUT/sound-BEF64-${channels}ch.wav --endian big $OUT/sound-BEF64-${channels}ch.raw
    
    # ADPCM files
    # NOTE: The SoX decoder may add extra frames when converting from adpcm to raw. Use `head` to truncate.
    # (The encoder appears to work correctly).
    sox $IN_WAV --encoding ima-adpcm --channels $channels $OUT/sound-ima-adpcm-${channels}ch.wav
    sox $OUT/sound-ima-adpcm-${channels}ch.wav --endian $system_endian --encoding signed-integer --bits 16 --channels $channels -t raw - \
        | head -c $(( ${IN_FRAMES[$channels - 1]} * $channels * 2)) \
        > $OUT/sound-ima-adpcm-${channels}ch.raw

    sox $IN_WAV --encoding ms-adpcm --channels $channels $OUT/sound-ms-adpcm-${channels}ch.wav
    sox $OUT/sound-ms-adpcm-${channels}ch.wav --endian $system_endian --encoding signed-integer --bits 16 --channels $channels -t raw - \
        | head -c $(( ${IN_FRAMES[$channels - 1]} * $channels * 2)) \
        > $OUT/sound-ms-adpcm-${channels}ch.raw
done

echo ${0##*/}: Done.
