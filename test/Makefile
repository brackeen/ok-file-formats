# Requires ImageMagick
#
CC = clang -O3 -std=c99 -Wall -Wextra -Wdocumentation -Wdocumentation-unknown-command

BIN = bin
GEN = gen
PNG_SUITE = PngSuite
JPG_SUITE = jpg
INCLUDES = -I .. 
FILES = *.c ../*.c
OUT_NAME = ok_file_formats_test
HAS_VALGRIND := $(shell type valgrind 1>/dev/null 2>&1 ; echo $$?)

# Get all image files
PNG_SUITE_FILES := $(wildcard $(PNG_SUITE)/*.png)
JPG_SUITE_FILES := $(wildcard $(JPG_SUITE)/*.jpg)
RGBA_FILES := $(patsubst $(PNG_SUITE)/%.png, $(GEN)/%.rgba, $(PNG_SUITE_FILES))
RGBA_FILES += $(patsubst $(JPG_SUITE)/%.jpg, $(GEN)/%.rgba, $(JPG_SUITE_FILES))

test: gen build
ifeq ($(HAS_VALGRIND),0)
	valgrind --tool=memcheck --leak-check=full $(BIN)/$(OUT_NAME)
else
	$(BIN)/$(OUT_NAME)
endif	
	
build:  
	$(CC) -o $(BIN)/$(OUT_NAME) -Wall $(INCLUDES) $(FILES)

# Convert png files to raw RGBA8888 format, ignoring the gAMA chunk.
# The "-intensity Average" setting prevents ImageMagick from 
# color-converting grayscale files.
# There will be warnings for files "oi9n0g16.png" and "oi9n2c16.png" but
# they will convert successfully.
$(GEN)/%.rgba: $(PNG_SUITE)/%.png
	-convert -define png:exclude-chunk=gAMA $< temp.png
	@if test -f temp.png; then \
		convert -depth 8 -intensity Average temp.png $@; \
		rm temp.png; \
	else \
        touch $@; \
	fi

# Convert jpg files to raw RGBA8888 format.
# Tested against jpeg 8d via ImageMagick.
# Use -auto-orient to handle Exif orientation.
$(GEN)/%.rgba: $(JPG_SUITE)/%.jpg
	convert -auto-orient $< $@

gen: init $(RGBA_FILES)

init:
	@mkdir -p $(BIN)
	@mkdir -p $(GEN)
	
clean:
	rm -rf $(GEN)
	rm -f $(BIN)/$(OUT_NAME)