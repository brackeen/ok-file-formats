#!/bin/bash
#
# Meant to be run from the parent directory.
#
# Requres afconvert and sox
#
# brew install sox --with-libsndfile
#
# afconvert can't write big-endian WAVE files, so use SoX to convert.
#
# SoX (14.4.2) doesn't create 64-bit floating-point CAF files correctly, 
# and it can't read floating-point CAF files correctly, so it can't be used exclusively.
#

echo Generating sounds...

IN=wav
OUT=gen

mkdir -p $OUT

caf_data_formats=( I8 BEI16 BEI24 BEI32 BEF32 BEF64 LEI16 LEI24 LEI32 LEF32 LEF64 )
wav_data_formats=( UI8 LEI16 LEI24 LEI32 LEF32 LEF64 )

# TODO:
# WAV: IMA-ADPCM 
# WAV: MS-ADPCM
# WAV: ulaw
# WAV: alaw 
# CAF: IMA4 
# CAF: ulaw
# CAF: alaw

for channels in `seq 1 2`;
do
    for data_format in "${caf_data_formats[@]}"
    do
        afconvert -f caff -c $channels -d $data_format $IN/sound.wav $OUT/sound-${data_format}-${channels}ch.caf
    done
    
    sox $OUT/sound-I8-${channels}ch.caf $OUT/sound-I8-${channels}ch.raw
    
    for data_format in "${wav_data_formats[@]}"
    do
        afconvert -f WAVE -c $channels -d $data_format $IN/sound.wav $OUT/sound-${data_format}-${channels}ch.wav
        sox $OUT/sound-${data_format}-${channels}ch.wav $OUT/sound-${data_format}-${channels}ch.raw
    done
    
    # Big endian WAVE files
    sox $IN/sound.wav --endian big --bits 16 --channels $channels $OUT/sound-BEI16-${channels}ch.wav
    sox $IN/sound.wav --endian big --bits 24 --channels $channels $OUT/sound-BEI24-${channels}ch.wav
    sox $IN/sound.wav --endian big --bits 32 --channels $channels $OUT/sound-BEI32-${channels}ch.wav
    sox $IN/sound.wav --endian big --encoding floating-point --bits 32 --channels $channels $OUT/sound-BEF32-${channels}ch.wav
    sox $IN/sound.wav --endian big --encoding floating-point --bits 64 --channels $channels $OUT/sound-BEF64-${channels}ch.wav
    
    sox $OUT/sound-BEI16-${channels}ch.wav $OUT/sound-BEI16-${channels}ch.raw
    sox $OUT/sound-BEI24-${channels}ch.wav $OUT/sound-BEI24-${channels}ch.raw
    sox $OUT/sound-BEI32-${channels}ch.wav $OUT/sound-BEI32-${channels}ch.raw
    sox $OUT/sound-BEF32-${channels}ch.wav $OUT/sound-BEF32-${channels}ch.raw
    sox $OUT/sound-BEF64-${channels}ch.wav $OUT/sound-BEF64-${channels}ch.raw

done

echo Done.
