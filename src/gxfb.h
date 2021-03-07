#ifndef GXFB_H_INCLUDED
#define GXFB_H_INCLUDED

#include <gccore.h>

#define RGB(r, g, b)                   ((255 << 24) | ((r) << 16) | ((g) << 8) | (b))

extern u32 *framebuffer;
extern int framebuffer_width;
extern int framebuffer_height;
extern int framebuffer_left;
extern int framebuffer_top;
extern int framebuffer_right;
extern int framebuffer_bottom;
extern size_t framebuffer_size;

int fb_init(GXRModeObj *rmode, int width, int height);
void fb_flip(int wait_vsync);

void fb_clear(u32 color);

static inline u32* fb_pixel_ptr(int x, int y) {
	return framebuffer + (y * framebuffer_width) + x;
}

static inline void fb_pset(int x, int y, u32 color) {
	*fb_pixel_ptr(x, y) = color;
}

static inline u32 fb_pget(int x, int y) {
	return *fb_pixel_ptr(x, y);
}

void fb_printf(int x, int y, u32 color, const char *format, ...);

#endif
