# ok-file-formats

C functions for reading a few different file formats. No external dependencies. Written in C99.

| Library            | Description
|--------------------|---------------------------------------------------------------------------------------------------
| [ok_png](ok_png.h) | Reads PNG files. Supports Apple's proprietary `CgBI` chunk. Tested against the PngSuite.
| [ok_jpg](ok_jpg.h) | Reads JPEG files. Baseline and progressive formats. Interprets EXIF orientation tags. No CMYK support.
| [ok_wav](ok_wav.h) | Reads WAV and CAF files. PCM, u-law, a-law, and ADPCM formats.
| [ok_fnt](ok_fnt.h) | Reads AngelCode BMFont files. Binary format from AngelCode Bitmap Font Generator v1.10 or newer.
| [ok_csv](ok_csv.h) | Reads Comma-Separated Values files.
| [ok_mo](ok_mo.h)   | Reads gettext MO files.

The source files do not depend on one another. If all you need is to read a PNG file, just
use `ok_png.h` and `ok_png.c`.

The `CMakeLists.txt` file can be used but is not required.

The `ok_png`, `ok_jpg`, and `ok_wav` functions include:
* Option to use a custom allocator (`ok_png_read_with_allocator`, etc.)
* Fuzz tests.

The `ok_png` and `ok_jpg` functions include these decode options:
* Get the image dimensions without decoding image data.
* Premultiply alpha.
* Flip the image vertically.

## Example: Decode PNG

```C
#include <stdio.h>
#include "ok_png.h"

int main() {
    FILE *file = fopen("my_image.png", "rb");
    ok_png image = ok_png_read(file, OK_PNG_COLOR_FORMAT_RGBA | OK_PNG_PREMULTIPLIED_ALPHA | OK_PNG_FLIP_Y);
    fclose(file);
    if (image.data) {
        printf("Got image! Size: %li x %li\n", (long)image.width, (long)image.height);
        free(image.data);
    }
    return 0;
}
```

## Recent breaking changes in `ok_png`, `ok_jpg`, and `ok_wav`:
* The read functions now return the `ok_png`, `ok_jpg`, and `ok_wav` structs on the stack instead of the heap. (These structs are small, around 24-32 bytes).
* Replaced `ok_png_read_to_buffer` with `ok_png_read_with_allocator`.
* Replaced `ok_png_read_from_callbacks` with `ok_png_read_from_input`.
* Replaced `error_message` with `error_code`.
* Removed `ok_png_free`. Free `png.data` directly instead.

