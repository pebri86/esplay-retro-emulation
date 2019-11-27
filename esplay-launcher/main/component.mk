#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)
COMPONENT_ADD_INCLUDEDIRS := include
COMPONENTS_EXTRA_CLEAN := gfxTile.inc gfxTile.raw
main.o: gfxTile.inc

gfxTile.inc: $(COMPONENT_PATH)/gfxTile.png
	ffmpeg -i $^ -f rawvideo -pix_fmt rgb565 gfxTile.raw -y
	cat gfxTile.raw | xxd -i > gfxTile.inc