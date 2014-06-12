#ok-file-formats
A few file format decoders:

* **PNG** - Reads any PNG format, including Apple's proprietary PNG extensions for iOS. Tested against images in the PngSuite.
* **WAV** - Reads WAV or CAF files. PCM format only. 
* **FNT** - Reads AngelCode bitmap font files. Binary format, version 3, from AngelCode Bitmap Font Generator v1.10 or newer.

## Example: Decode PNG and upload to OpenGL
This example decodes a PNG and uploads the data to OpenGL. The color format is optimized on iOS devices.

```C
#if GL_APPLE_texture_format_BGRA8888 && GL_BGRA_EXT
GLenum glFormat = GL_BGRA_EXT;
ok_color_format okFormat = OK_COLOR_FORMAT_BGRA_PRE;
#else
GLenum glFormat = GL_RGBA;
ok_color_format okFormat = OK_COLOR_FORMAT_RGBA_PRE;
#endif
bool flipY = true;     
ok_image *image = ok_png_read("my_image.png", okFormat, flipY);
if (image->data == NULL) {
    printf("Error: %s\n", image->error_message);
}
else {
    GLuint textureId;
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
```

## License
[ZLIB](http://en.wikipedia.org/wiki/Zlib_License)