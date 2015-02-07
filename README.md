#ok-file-formats
A few file format decoders. No external dependencies.

* **PNG** - Reads any PNG file. 
* **JPG** - Reads most JPEG files. Baseline only, no progressive support.
* **WAV** - Reads WAV or CAF files. PCM format only. 
* **FNT** - Reads AngelCode BMFont files.
* **CSV** - Reads Comma-Separated Values files. 
* **MO** - Reads gettext MO files. 

The files do not depend on one another, and there are no dependencies on external libraries. If all you need is to read a PNG file, just grab `ok_png.h` and `ok_png.c` and you're good to go.


## Example: Decode PNG and upload to OpenGL
This example decodes a PNG from a stdio file and uploads the data to OpenGL. The color format is optimized for iOS devices.

```C
#include "ok_png.h"

static int file_input_func(void *user_data, unsigned char *buffer, const int count) {
    FILE *fp = (FILE *)user_data;
    if (buffer && count > 0) {
        return (int)fread(buffer, 1, count, fp);
    }
    else if (fseek(fp, count, SEEK_CUR) == 0) {
        return count;
    }
    else {
        return 0;
    }
}

GLuint load_texture(const char *file_name, const bool flip_y) {
#if GL_APPLE_texture_format_BGRA8888 && GL_BGRA_EXT
    GLenum glFormat = GL_BGRA_EXT;
    ok_color_format ok_format = OK_COLOR_FORMAT_BGRA_PRE;
#else
    GLenum glFormat = GL_RGBA;
    ok_color_format ok_format = OK_COLOR_FORMAT_RGBA_PRE;
#endif
    FILE *fp = fopen(file_name, "rb");
    ok_image *image = ok_png_read(fp, file_input_func, ok_format, flip_y);
    fclose(fp);
    
    GLuint textureId = 0;
    if (!image->data) {
        printf("Error: %s\n", image->error_message);
    }
    else {
        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image->width, image->height, 0,
                     glFormat, GL_UNSIGNED_BYTE, image->data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    ok_image_free(image);
    return textureId;
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