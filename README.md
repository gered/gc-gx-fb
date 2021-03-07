# Gamecube GX-based RGB Framebuffer Example

Just a simple example of using a GX ARGB-format texture as a means to render a "software" pixel framebuffer
quickly. Most applications won't care about such a thing, but this can be nice if you want low-level per-pixel access
in your code (e.g. MS-DOS Mode 13h style).

If you want to access pixels using RGB, this is significantly better than using the XFB/EFB buffer, where the pixel
data uses YUV color space.

Probably there could be a number of improvements made to this ... I'm not well versed in the GX API.
