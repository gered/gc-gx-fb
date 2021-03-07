#include "gxfb.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "vgafont.h"

#define DEFAULT_FIFO_SIZE              256 * 1024

u32 *framebuffer = NULL;
int framebuffer_width = 0;
int framebuffer_height = 0;
int framebuffer_left;
int framebuffer_top;
int framebuffer_right;
int framebuffer_bottom;
size_t framebuffer_size = 0;

static u32 *xfb[2] = { NULL, NULL };
static int current_xfb = 0;
static void *gpfifo = NULL;

static size_t texture_image_size = 0;
static u32 *texture_image = NULL;
static GXTexObj texture;

static int quad_x_offset = 0;
static int quad_y_offset = 0;

static Mtx44 perspective;
static Mtx modelview;

static char printf_buffer[1024];

static void gx_copyxfb(void) {
	GX_CopyDisp(xfb[current_xfb], GX_TRUE);
	VIDEO_SetNextFramebuffer(xfb[current_xfb]);
	current_xfb ^= 1;
}

static void video_init(GXRModeObj *rmode) {
	VIDEO_Init();

	xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb[0]);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();

	current_xfb = 1;
}

static void gx_init(GXRModeObj *rmode, GXColor clearColor) {
	gpfifo = aligned_alloc(32, DEFAULT_FIFO_SIZE);
	memset(gpfifo, 0, DEFAULT_FIFO_SIZE);

	GX_Init(gpfifo, DEFAULT_FIFO_SIZE);

	GX_SetCopyClear(clearColor, GX_MAX_Z24);

	GX_InvVtxCache();
	GX_ClearVtxDesc();

	// define various view properties, like the viewport, scissor(clipping), efb/xfb dimensions, filters ...
	GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
	GX_SetDispCopyYScale((f32) rmode->xfbHeight / (f32) rmode->efbHeight);
	GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopyDst(rmode->fbWidth, rmode->xfbHeight);
	GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
	GX_SetFieldMode(rmode->field_rendering, ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));

	if (rmode->aa)
		GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
	else
		GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

	GX_SetCullMode(GX_CULL_NONE);
	gx_copyxfb();
	GX_SetDispCopyGamma(GX_GM_1_0);

	GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
	GX_SetColorUpdate(GX_TRUE);
}

static void gx_init_texture(int width, int height) {
	texture_image_size = width * height * 4;
	texture_image = aligned_alloc(32, texture_image_size);
	memset(texture_image, 0, texture_image_size);

	GX_InvalidateTexAll();
	GX_InitTexObj(&texture, texture_image, width, height, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjFilterMode(&texture, GX_NEAR, GX_NEAR);
	GX_LoadTexObj(&texture, GX_TEXMAP0);
}

static void gx_init_vertex_format(void) {
	// 2D X/Y + texture coordinates

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GX_SetNumChans(1);
	GX_SetNumTexGens(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
}

static void gx_init_projection(GXRModeObj *rmode) {
	// TODO: this is really only going to be appropriate for 240p-equivalent modes ...
	guOrtho(perspective, 0, rmode->efbHeight - 1, 0, (rmode->fbWidth / 2) - 1, 0, 300);
	GX_LoadProjectionMtx(perspective, GX_ORTHOGRAPHIC);
}

static void gx_init_modelview(void) {
	// set up constant modelview matrix for rending our quad with
	guMtxIdentity(modelview);
	guMtxTransApply(modelview, modelview, 0.0, 0.0, -5.0);
	GX_LoadPosMtxImm(modelview, GX_PNMTX0);
}

static void copy_framebuffer_to_texture(void) {
	/*
	 gamecube/wii texture memory is organized in 32-byte tiles. different pixel formats will allow for a varying
	 amount of pixels to be contained within each 32-byte tile. for 32-bit ARGB-format textures, as we are using,
	 this is slightly more complicated ...

	 basically, in our case we need to write *TWO* 32-byte tiles in succession. for 32-bit ARGB-format textures, the
	 gamecube assumes that the first 32-byte tile will contain a 4x4 pixel area where the pixels are 16-bit only and
	 contain only the alpha+red components. the second 32-byte tile will be for the same 4x4 pixel area, but will
	 contain 16-bit pixels containing only the green+blue components
	*/

	// reading 4 source ARGB-format pixel rows per iteration
	u16 *src_1 = (u16*)&framebuffer[0];
	u16 *src_2 = (u16*)&framebuffer[framebuffer_width];
	u16 *src_3 = (u16*)&framebuffer[framebuffer_width * 2];
	u16 *src_4 = (u16*)&framebuffer[framebuffer_width * 3];

	// destination 16-bit AR-format (alpha+red) pixel destination rows
	u16 *dst_ar_1 = (u16*)&texture_image[0];
	u16 *dst_ar_2 = (u16*)&texture_image[8/4];
	u16 *dst_ar_3 = (u16*)&texture_image[16/4];
	u16 *dst_ar_4 = (u16*)&texture_image[24/4];

	// destination 16-bit GB-format (green+blue) pixel destination rows
	u16 *dst_gb_1 = (u16*)&texture_image[32/4];
	u16 *dst_gb_2 = (u16*)&texture_image[40/4];
	u16 *dst_gb_3 = (u16*)&texture_image[48/4];
	u16 *dst_gb_4 = (u16*)&texture_image[56/4];

	// work through the framebuffer in 4x4 pixel chunks
	for (int y = 0; y < framebuffer_height; y += 4) {
		for (int x = 0; x < framebuffer_width; x += 4) {
			// each loop iteration copies an entire 4x4 pixel chunk

			// column 1 for all 4 rows
			dst_ar_1[0] = src_1[0]; dst_gb_1[0] = src_1[1];
			dst_ar_2[0] = src_2[0]; dst_gb_2[0] = src_2[1];
			dst_ar_3[0] = src_3[0]; dst_gb_3[0] = src_3[1];
			dst_ar_4[0] = src_4[0]; dst_gb_4[0] = src_4[1];

			// column 2 for all 4 rows
			dst_ar_1[1] = src_1[2]; dst_gb_1[1] = src_1[3];
			dst_ar_2[1] = src_2[2]; dst_gb_2[1] = src_2[3];
			dst_ar_3[1] = src_3[2]; dst_gb_3[1] = src_3[3];
			dst_ar_4[1] = src_4[2]; dst_gb_4[1] = src_4[3];

			// column 3 for all 4 rows
			dst_ar_1[2] = src_1[4]; dst_gb_1[2] = src_1[5];
			dst_ar_2[2] = src_2[4]; dst_gb_2[2] = src_2[5];
			dst_ar_3[2] = src_3[4]; dst_gb_3[2] = src_3[5];
			dst_ar_4[2] = src_4[4]; dst_gb_4[2] = src_4[5];

			// column 4 for all 4 rows
			dst_ar_1[3] = src_1[6]; dst_gb_1[3] = src_1[7];
			dst_ar_2[3] = src_2[6]; dst_gb_2[3] = src_2[7];
			dst_ar_3[3] = src_3[6]; dst_gb_3[3] = src_3[7];
			dst_ar_4[3] = src_4[6]; dst_gb_4[3] = src_4[7];

			// move right to the next 4x4 tile for source and dest

			src_1 += 8;
			src_2 += 8;
			src_3 += 8;
			src_4 += 8;

			dst_ar_1 += 32;
			dst_ar_2 += 32;
			dst_ar_3 += 32;
			dst_ar_4 += 32;
			dst_gb_1 += 32;
			dst_gb_2 += 32;
			dst_gb_3 += 32;
			dst_gb_4 += 32;
		}

		// move down to the next 4x4 tile row for source (dest will be correct already)
		src_1 += (framebuffer_width * 2) * 3;
		src_2 += (framebuffer_width * 2) * 3;
		src_3 += (framebuffer_width * 2) * 3;
		src_4 += (framebuffer_width * 2) * 3;
	}
}

int fb_init(GXRModeObj *rmode, int width, int height) {
	if (framebuffer)
		return 1;
	if (!rmode)
		return 1;
	if (width <= 0 || width > (rmode->fbWidth / 2))
		return 1;
	if (width % 4)
		return 1;
	if (height <= 0 || height > rmode->efbHeight)
		return 1;
	if (height % 4)
		return 1;

	video_init(rmode);

	GXColor clearColor = { .r = 0, .g = 0, .b = 0, .a = 0xff };
	gx_init(rmode, clearColor);

	gx_init_texture(width, height);
	gx_init_vertex_format();
	gx_init_projection(rmode);
	gx_init_modelview();

	// allocate application-accessible framebuffer. ARGB-format pixels
	framebuffer_size = width * height * 4;
	framebuffer = aligned_alloc(32, framebuffer_size);
	framebuffer_width = width;
	framebuffer_height = height;
	framebuffer_left = 0;
	framebuffer_top = 0;
	framebuffer_right = framebuffer_width - 1;
	framebuffer_bottom = framebuffer_height - 1;
	memset(framebuffer, 0, framebuffer_size);

	// x/y offset for rending the quad.
	// this will center it on screen if the framebuffer is smaller than the screen mode
	quad_x_offset = ((rmode->fbWidth / 2) - width) / 2;
	quad_y_offset = ((rmode->efbHeight) - height) / 2;

	return 0;
}

void fb_flip(int wait_vsync) {
	// update the texture's pixel data with the contents of the application-accessible framebuffer
	copy_framebuffer_to_texture();
	DCFlushRange(texture_image, texture_image_size);

	// render the framebuffer-textured quad
	GX_InvVtxCache();
	GX_InvalidateTexAll();

	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	GX_Position2f32(quad_x_offset, quad_y_offset);
	GX_TexCoord2f32(0.0, 0.0);
	GX_Position2f32(quad_x_offset + framebuffer_width - 1, quad_y_offset);
	GX_TexCoord2f32(1.0, 0.0);
	GX_Position2f32(quad_x_offset + framebuffer_width - 1, quad_y_offset + framebuffer_height - 1);
	GX_TexCoord2f32(1.0, 1.0);
	GX_Position2f32(quad_x_offset, quad_y_offset + framebuffer_height - 1);
	GX_TexCoord2f32(0.0, 1.0);
	GX_End();

	GX_DrawDone();

	gx_copyxfb();
	GX_Flush();

	VIDEO_Flush();
	if (wait_vsync) {
		VIDEO_WaitVSync();
	}
}

void fb_clear(u32 color) {
	for (u32 i = 0; i < framebuffer_size; ++i) {
		framebuffer[i] = color;
	}
}

static void blit_char(char c, int x, int y, u32 color) {
	const u8 *work_char;
	u8 bit_mask = 0x80;
	u32 *ptr;

	work_char = &vgafont[(unsigned char)c * 8];

	ptr = fb_pixel_ptr(x, y);

	for (int yc = 0; yc < 8; ++yc) {
		bit_mask = 0x80;
		for (int xc = 0; xc < 8; ++xc) {
			if ((*work_char & bit_mask))
				ptr[xc] = color;

			bit_mask = (bit_mask >> 1);
		}

		ptr += framebuffer_width;
		++work_char;
	}
}

void fb_printf(int x, int y, u32 color, const char *format, ...) {
	va_list args;
	va_start(args, format);
	vsnprintf(printf_buffer, 1023, format, args);
	va_end(args);

	printf_buffer[1023] = 0;

	int dx = x;
	int dy = y;

	for (char *c = printf_buffer; *c; ++c) {
		switch (*c) {
			case '\n':
				dx = x;
				dy += 8;
				break;
			case '\r':
				break;
			case ' ':
				dx += 8;
				break;
			default:
				blit_char(*c, dx, dy, color);
				dx += 8;
				break;
		}
	}
}
