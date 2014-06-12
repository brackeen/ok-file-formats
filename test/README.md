Tests if the output of ok_png decoder matches that of the libpng decoder.

Uses the [PngSuite](http://www.schaik.com/pngsuite/pngsuite.html) images from Willem van Schaik.

Requires ImageMagick. On Mac OS X, if you have [Homebrew](http://brew.sh/) installed:

        brew install imagemagick

The Makefile uses ImageMagick (which uses libpng) to convert all the png images to raw RGBA data. The raw RGBA data is then used for comparison. 

Note, during the conversion, the PNG gAMA chunk is ignored, because ok_png doesn't support the gAMA chunk. So, technically, the tests are a bit fudged.

The iOS version exists to test Apple's proprietary PNG extensions. It must be run on an actual iOS device, and not the simulator. The Makefile must be run first, to generate the raw RGBA data for comparison.

The Emscripten version exists as a sanity check.