# ok-file-formats tests

There are two tests here: A correctness test and a fuzz test.

The correctness test checks the output of `ok_png`, `ok_jpg`, and `ok_wav` against the output of
[ImageMagick 7](https://www.imagemagick.org/), [SoX 14](http://sox.sourceforge.net/)) and, on macOS,
the pre-installed `afconvert`.

The fuzz test uses American Fuzzy Lop++.

## Requirements

On macOS, install ImageMagick 7 and SoX:

    brew install imagemagick sox

For the fuzz test, install American Fuzzy Lop++:

    brew install afl++

## Correctness test

To run the correctness test:

    cmake -B build && cmake --build build && ctest --verbose --test-dir build

### PNG

For PNG tests, the [PngSuite](http://www.schaik.com/pngsuite/pngsuite.html) images from Willem van
Schaik are used.

Tested against ImageMagick 7.0.8-23. Earlier versions of ImageMagick 7 may have incorrect output for
some PNG files.

To test Apple's proprietary PNG extensions, the tests must run on an actual iOS device, and not the
simulator.

### JPEG

The JPEG tests disable "fancy upsampling" because results will vary based on what version of the
libjpeg library ImageMagick uses. The `ok_jpg` decoder uses a similar upsampling method than the
libjpeg-8d library, but is different that libjpeg-turbo.

Although there are only a few dozen JPEGs in the included tests, `ok_jpg` has been tested against
thousands of images, some dated back to the 90's.

## Fuzz test

To build `ok-file-formats-fuzz-test`:

    cmake -B build -DFUZZ_TEST=ON && cmake --build build

Optionally, enable Address Sanitizer (warning, this is slow):

    cmake -B build -DFUZZ_TEST=ON -DFUZZ_WITH_ASAN=ON && cmake --build build

Then run one of these commands:

    afl-fuzz -i build/gen/fuzzing/input/wav -o build/gen/fuzzing/afl_results/wav ./build/ok-file-formats-fuzz-test --wav
    afl-fuzz -i build/gen/fuzzing/input/caf -o build/gen/fuzzing/afl_results/caf ./build/ok-file-formats-fuzz-test --caf
    afl-fuzz -t 1000 -x png.dict -i build/gen/fuzzing/input/png -o build/gen/fuzzing/afl_results/png ./build/ok-file-formats-fuzz-test --png
    afl-fuzz -t 1000 -x jpg.dict -i build/gen/fuzzing/input/jpg -o build/gen/fuzzing/afl_results/jpg ./build/ok-file-formats-fuzz-test --jpg

Fuzzing will take hours or even days to complete, depending on the input. `afl-fuzz` runs on one
core, so if you have a multi-core machine you can run multiple tests at once.

The generated input cases are imperfect. For more input cases, see
[Mozilla's Fuzzdata samples](https://github.com/MozillaSecurity/fuzzdata/tree/master/samples) and
[input cases provided by moonAgirl](https://github.com/moonAgirl/Bugs/tree/master/ok-file-formats).
