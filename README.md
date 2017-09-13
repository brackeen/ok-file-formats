# ok-file-formats

C functions for reading a few different file formats. No external dependencies.

| Library            | Description
|--------------------|---------------------------------------------------------------------------------------------------
| [ok_png](ok_png.h) | Reads PNG files. Supports Apple's proprietary `CgBI` chunk. Tested against the PngSuite.
| [ok_jpg](ok_jpg.h) | Reads JPEG files. Baseline and progressive formats. Interprets EXIF orientation tags. No CMYK support.
| [ok_wav](ok_wav.h) | Reads WAV and CAF files. PCM, u-law, a-law, and ADPCM formats.
| [ok_fnt](ok_fnt.h) | Reads AngelCode BMFont files. Binary format from AngelCode Bitmap Font Generator v1.10 or newer.
| [ok_csv](ok_csv.h) | Reads Comma-Separated Values files.
| [ok_mo](ok_mo.h)   | Reads gettext MO files.

The source files do not depend on one another. If all you need is to read a PNG file, just
grab `ok_png.h` and `ok_png.c`.

The image loading functions in `ok_png` and `ok_jpg` include the following options:
* Get the image dimensions without decoding image data.
* Load image data into preallocated buffers.
* Premultiply alpha.
* Flip the image vertically.

## Example: Decode PNG

```C
#include <stdio.h>
#include "ok_png.h"

int main() {
    FILE *file = fopen("my_image.png", "rb");
    ok_png *image = ok_png_read(file, OK_PNG_COLOR_FORMAT_RGBA | OK_PNG_PREMULTIPLIED_ALPHA | OK_PNG_FLIP_Y);
    fclose(file);
    if (image->data) {
        printf("Got image! Size: %li x %li\n", (long)image->width, (long)image->height);
    }
    ok_png_free(image);
    return 0;
}
```
