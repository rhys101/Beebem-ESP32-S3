# Launcher screenshots

Local PNG/JPEG files in this directory may be prepared from screenshots that
the builder is entitled to use. They are not part of the public repository.

`tools/prepare_screenshots.py` nearest-neighbour scales each image from the
archive's 640x510/512 raster to bc32's 640x256 indexed framebuffer and emits a
compact `.rle` file. Each RLE byte stores a three-bit BBC colour and a run of
one to 32 pixels. Only the `.rle` files are embedded in firmware.

Screenshots remain subject to the original artwork copyrights and must not be
assumed redistributable merely because they are available online.
