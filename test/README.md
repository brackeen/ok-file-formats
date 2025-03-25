# ok-file-formats tests

The `ok_png`, `ok_jpg`, and `ok_wav` decoders are tested against the output of other tools: [ImageMagick 7](https://www.imagemagick.org/) and [SoX 14](http://sox.sourceforge.net/). Additionally, on macOS, the pre-installed `afconvert` is used to test `CAF` decoding.

On macOS, if you have [Homebrew](http://brew.sh/) installed:

    brew install imagemagick sox

## PNG

For PNG tests, the [PngSuite](http://www.schaik.com/pngsuite/pngsuite.html) images from Willem van Schaik are used.

Tested against ImageMagick 7.0.8-23. Earlier versions of ImageMagick 7 may have incorrect output for some PNG files.

To test Apple's proprietary PNG extensions, the tests must run on an actual iOS device, and not the simulator.

## JPEG

The JPEG results will vary based on what version of the IJG library ImageMagick uses. Best results are with the IJG jpeg-8d library. Although there are only a few dozen JPEGs in the included tests, I've also tested the decoder on thousands of images, some dated back to the 90's.

## Test on macOS and Linux

    cmake -B build && cmake --build build && ctest --verbose --test-dir build

## Test on Windows using PowerShell

    mkdir build
    cd build
    cmake ..
    cmake --build .
    ctest -C Debug --verbose

## Test with a fuzzer

American Fuzzy Lop can be used for fuzz testing.

Install American Fuzzy Lop on macOS:

    brew install afl-fuzz

Then build the test as usual, which builds `ok-file-formats-fuzzing` and generates some test input files:

    mkdir build && cd build
    cmake .. && cmake --build .

Optionally, the `ok-file-formats-fuzzing` binary can be built with Address Sanitizer by setting `FUZZ_WITH_ASAN` to `ON`. This is slow, however. (Thanks to [WayneDevMaze](https://github.com/WayneDevMaze) for the idea to fuzz with Address Sanitizer.)

Then run one of these commands:

    afl-fuzz -i gen/fuzzing/input/wav -o gen/fuzzing/afl_results/wav ./ok-file-formats-fuzzing --wav
    afl-fuzz -i gen/fuzzing/input/caf -o gen/fuzzing/afl_results/caf ./ok-file-formats-fuzzing --caf
    afl-fuzz -t 1000 -x ../png.dict -i gen/fuzzing/input/png -o gen/fuzzing/afl_results/png ./ok-file-formats-fuzzing --png
    afl-fuzz -t 1000 -x ../jpg.dict -i gen/fuzzing/input/jpg -o gen/fuzzing/afl_results/jpg ./ok-file-formats-fuzzing --jpg

Fuzzing will take hours or even days to complete, depending on the input. `afl-fuzz` runs on one core, so if you have a multi-core machine you can run multiple tests at once.

The generated input cases are imperfect. For more input cases, see [Mozilla's Fuzzdata samples](https://github.com/MozillaSecurity/fuzzdata/tree/master/samples) and [input cases provided by moonAgirl](https://github.com/moonAgirl/Bugs/tree/master/ok-file-formats).
