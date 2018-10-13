# ok-file-formats tests

The `ok_png`, `ok_jpg`, and `ok_wav` decoders are tested against the output of other tools: [ImageMagick 7](https://www.imagemagick.org/) and [SoX 14](http://sox.sourceforge.net/). Additionally, on macOS, the pre-installed `afconvert` is used to test `CAF` decoding.

On macOS, if you have [Homebrew](http://brew.sh/) installed:

    brew install imagemagick
    brew install sox --with-libsndfile

## PNG

For PNG tests, the [PngSuite](http://www.schaik.com/pngsuite/pngsuite.html) images from Willem van Schaik are used.

At the moment, some of the PNG tests are failing when tested against ImageMagick 7.0.8-12 (2- and 4-bit grayscale PNGs). These are false negatives - `ok_png` is correct, and either ImageMagick or an underlying library ImageMagick uses are incorrect.

To test Apple's proprietary PNG extensions, the tests must run on an actual iOS device, and not the simulator.

## JPEG

The JPEG results will vary based on what version of the IJG library ImageMagick uses. Best results are with the IJG jpeg-8d library. Although there are only a few dozen JPEGs in the included tests, I've also tested the decoder on thousands of images, some dated back to the 90's.

## Test on macOS and Linux

    mkdir build
    cd build
    cmake .. && cmake --build . && ctest --verbose

## Test on Windows using PowerShell

    mkdir build
    cd build
    cmake ..
    cmake --build .
    ctest -C Debug --verbose
