#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <debug.h>

#include "gxfb.h"

int main(int argc, char *argv[]) {
	//DEBUG_Init(GDBSTUB_DEVICE_USB, 1);
	//_break();

	GXRModeObj *rmode;
	s32 videoFormat = CONF_GetVideo();
	if (videoFormat == CONF_VIDEO_NTSC)
		rmode = &TVNtsc240Ds;
	else if (videoFormat == CONF_VIDEO_PAL)
		rmode = &TVPal264Ds;
	else if (videoFormat == CONF_VIDEO_MPAL)
		rmode = &TVMpal240Ds;
	else
		return 1;

	if (fb_init(rmode, 320, 240))
		return 1;

	PAD_Init();

	int x = 100, y = 100;

	while (1) {
		PAD_ScanPads();

		u32 pressed = PAD_ButtonsDown(0);
		u32 held = PAD_ButtonsHeld(0);

		if (pressed & PAD_BUTTON_START) {
			exit(0);
		}

		if (held & PAD_BUTTON_UP) {
			--y;
			if (y < 0)
				y = 0;
		}
		if (held & PAD_BUTTON_DOWN) {
			++y;
			if (y > framebuffer_bottom)
				y = framebuffer_bottom;
		}
		if (held & PAD_BUTTON_LEFT) {
			--x;
			if (x < 0)
				x = 0;
		}
		if (held & PAD_BUTTON_RIGHT) {
			++x;
			if (x > framebuffer_right)
				x = framebuffer_right;
		}

		fb_clear(RGB(32, 64, 128));

		fb_printf(30, 30, RGB(255, 255, 0), "hello, world!");
		fb_printf(30, 40, RGB(255, 0, 255), "x = %d, y = %d\n", x, y);

		fb_pset(x, y, RGB(255, 255, 255));

		fb_flip(true);
	}

	return 0;
}
