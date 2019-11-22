#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)
COMPONENT_ADD_INCLUDEDIRS := .
COMPONENTS_EXTRA_CLEAN := gfxTile.inc gfxTile.raw
main.o: gfxTile.inc

gfxTile.inc: $(COMPONENT_PATH)/gfxTile.png
	# using linux environtment
	#convert $^ -background none -layers flatten -crop 80x142+0+0 graphics.rgba
	# using mingw32 environment
	ffmpeg -i $^ -f rawvideo -pix_fmt rgb565 gfxTile.raw
	cat gfxTile.raw | xxd -i > gfxTile.inc