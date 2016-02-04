#ok-file-formats
A few file format decoders. No external dependencies.

* **PNG** - Reads any PNG file. 
* **JPG** - Reads most JPEG files. Baseline only, no progressive support.
* **WAV** - Reads WAV or CAF files. PCM format only. 
* **FNT** - Reads AngelCode BMFont files.
* **CSV** - Reads Comma-Separated Values files. 
* **MO** - Reads gettext MO files. 

The files do not depend on one another, and there are no dependencies on external libraries. If all you need is to read a PNG file, just grab `ok_png.h` and `ok_png.c` and you're good to go.


## Example: Decode PNG


```C
#include <stdio.h>
#include "ok_png.h"

static int file_input_func(void *user_data, uint8_t *buffer, const int count) {
    FILE *fp = (FILE *)user_data;
    if (buffer && count > 0) {
        return (int)fread(buffer, 1, (size_t)count, fp);
    } else if (fseek(fp, count, SEEK_CUR) == 0) {
        return count;
    } else {
        return 0;
    }
}

int main() {
    FILE *fp = fopen("my_image.png", "rb");
    ok_png *image = ok_png_read(fp, file_input_func, OK_PNG_COLOR_FORMAT_RGBA, false);
    fclose(fp);
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
* Reads WAV or CAF files. 
* PCM format only.

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