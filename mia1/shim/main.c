/*
 * Xiaomi Mi A1 (tissot) boot shim, first milestone: show that it runs.
 *
 * aboot leaves the panel running in video mode from its splash framebuffer
 * (cont-splash, 0x90001000 in the SoC's device tree): what is written there is
 * on the screen. Not yet known: its pixel format and row pitch. This fills the
 * whole region white, then draws eight colour bands assuming 1080 pixels of
 * three bytes per row. Straight horizontal bands: that guess is right.
 * Slanted bands: it is not, and their slant tells the real pitch.
 */
#include <stdint.h>

#define SPLASH_BASE   0x90001000UL
#define SPLASH_BYTES  0x013FF000UL
#define WIDTH         1080U
#define HEIGHT        1920U
#define BPP           3U

static void
fill(volatile uint8_t *p, uint64_t n, uint8_t v)
{
	for (uint64_t i = 0; i < n; i++)
		p[i] = v;
}

void
shim_main(uint64_t dtb)
{
	volatile uint8_t *fb = (volatile uint8_t *)SPLASH_BASE;
	/* blue, green, red order as LK's RGB888 layout stores them; bands top to bottom */
	static const uint8_t band[8][3] = {
		{ 0x00, 0x00, 0xff }, { 0x00, 0xff, 0x00 }, { 0xff, 0x00, 0x00 }, { 0x00, 0xff, 0xff },
		{ 0xff, 0x00, 0xff }, { 0xff, 0xff, 0x00 }, { 0x00, 0x00, 0x00 }, { 0x80, 0x80, 0x80 },
	};

	(void)dtb;
	fill(fb, SPLASH_BYTES, 0xff);
	for (uint32_t y = 0; y < HEIGHT; y++) {
		const uint8_t *c = band[y * 8 / HEIGHT];
		volatile uint8_t *row = fb + (uint64_t)y * WIDTH * BPP;
		/* the left 64 pixels stay white: a marker for where a row starts */
		for (uint32_t x = 64; x < WIDTH; x++) {
			row[x * BPP + 0] = c[0];
			row[x * BPP + 1] = c[1];
			row[x * BPP + 2] = c[2];
		}
	}
}
