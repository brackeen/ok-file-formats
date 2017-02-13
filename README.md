# ok-file-formats

C functions for reading a few different file formats. No external dependencies.

* **PNG** - Reads any PNG file.
* **JPG** - Reads most JPEG files. Baseline only, no progressive support.
* **WAV** - Reads WAV and CAF files.
* **FNT** - Reads AngelCode BMFont files.
* **CSV** - Reads Comma-Separated Values files.
* **MO** - Reads gettext MO files.

The files do not depend on one another. If all you need is to read a PNG file, just
grab `ok_png.h` and `ok_png.c` and you're good to go.

## Example: Decode PNG

```C
#include <stdio.h>
#include "ok_png.h"

int main() {
    FILE *file = fopen("my_image.png", "rb");
    ok_png *image = ok_png_read(file, OK_PNG_COLOR_FORMAT_RGBA, false);
    fclose(file);
    if (image->data) {
        printf("Got image! Size: %li x %li\n", (long)image->width, (long)image->height);
    }
    ok_png_free(image);
    return 0;
}
```

## More Info
### ok_png
* Reads any PNG file.
* All color formats, all bit depths, all transparency types.
* Complex formats like interlacing and multiple `IDAT` chunks.
* Reads Apple's proprietary `CgBI` chunk for iOS devices.
* Ignores `gAMA` chunks.
* Option to get the image dimensions without decoding.
* Options to premultiply alpha and flip the image vertically.
* Tested against the PngSuite.

### ok_jpg
* Reads most JPEG files.
* Baseline only (no progressive JPEGs)
* Interprets EXIF orientation tags.
* Option to get the image dimensions without decoding.
* Option to flip the image vertically.
* Tested with several JPEG files against IJG's jpeg-8d library.

### ok_wav
* Reads both WAV and CAF files.
* Supported encodings:
  * PCM (including floating-point).
  * u-law, a-law.
  * CAF: Apple's IMA ADPCM.
  * WAV: Microsoft's IMA ADPCM.
  * WAV: Microsoft's ADPCM.
* If the encoding of the file is u-law, a-law, or ADPCM, the data is converted to 16-bit signed integer PCM data.

### ok_fnt
* Reads AngelCode BMFont files.
* Binary format, version 3, from AngelCode Bitmap Font Generator v1.10 or newer.

### ok_csv
* Reads Comma-Separated Values files.
* Properly handles escaped fields.

### ok_mo
* Reads gettext MO files.
* Provides utility functions to convert UTF-8 to 32-bit Unicode.


## License
[ZLIB](http://en.wikipedia.org/wiki/Zlib_License)
