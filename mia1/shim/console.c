/*
 * Xiaomi Mi A1 (tissot) boot shim: the display, and text on it.
 *
 * aboot leaves the panel running in video mode, scanned out by the display
 * processor (MDP 5) from its splash memory at 0x90001000: 1080 x 1920, three
 * bytes a pixel in the order blue, green, red, 3240 bytes a row (what the
 * first test image, mia1-hello.img, showed). The XNU loader and the kernel
 * want four bytes a pixel, so the source pipe that reads the splash memory --
 * found by its address among the three LK may use -- is switched to XRGB8888
 * with 4320 bytes a row: one format register, its unpack pattern, the stride,
 * and a flush that takes effect at the next frame. Same memory, same panel
 * timing; nothing about the panel itself is touched.
 *
 * Register layouts: LK (lk2nd) platform/msm_shared/mdp5.c and include/mdp5.h,
 * platform/msm8953/include/platform/iomap.h; Linux drivers/gpu/drm/msm/disp/
 * mdp_format.c for the XRGB8888 format word.
 */
#include "shim.h"
#include "font5x12.h"

#define MDP_BASE                0x01a00000UL
#define MDP_CTL0_FLUSH          (MDP_BASE + 0x2000 + 0x18)
#define SSPP_SRC0_ADDR          0x14
#define SSPP_SRC_YSTRIDE        0x24
#define SSPP_SRC_FORMAT         0x30
#define SSPP_SRC_UNPACK         0x34
#define FORMAT_RGB888           0x0002243fu     /* LK's */
#define FORMAT_XRGB8888         0x000236ffu     /* 4 components of 8 bits, 4 bytes, 4 unpacked, tight */
#define UNPACK_XRGB8888         0x03020001u     /* bytes in memory: blue, green, red, x */
#define FLUSH_VIG0              (1u << 0)
#define FLUSH_LM0               (1u << 6)
#define FLUSH_CTL               (1u << 17)

/*
 * The scaled mode (bootargs fbw=W fbh=H, e.g. 750 x 1624: the iPhone X's screen at two pixels a
 * point, what the kernel's fbscale=2 draws): the picture is drawn at that size and the VIG0 pipe
 * -- the only one of this MDP with a scaler (Linux mdp5_cfg.c, msm8x53_config) -- reads it and
 * scales it to the panel, bilinear, in place of the RGB pipe LK set up: the processor neither
 * draws the panel's pixels nor works them out. Its source
 * registers as Linux's mdp5_plane.c writes them; on this MDP (1.16) a pipe's buffer is its own
 * (LK's has_fixed_size_smp()), so there is nothing to allocate.
 */
#define CTL0_LAYER0             (MDP_BASE + 0x2000 + 0x00)
#define VIG0                    (MDP_BASE + 0x05000)
#define SSPP_SRC_SIZE           0x00
#define SSPP_SRC_IMG_SIZE       0x04
#define SSPP_SRC_XY             0x08
#define SSPP_OUT_SIZE           0x0c
#define SSPP_OUT_XY             0x10
#define SSPP_SRC_OP_MODE        0x38
#define SSPP_REQPRIO_WM(n)      (0x50 + 4 * (n))
#define SSPP_SRC_ADDR_SW_STATUS 0x70
#define SSPP_DECIMATION         0xb4
#define SSPP_PIX_EXT_LR(c)      (0x100 + 0x10 * (c))
#define SSPP_PIX_EXT_TB(c)      (0x104 + 0x10 * (c))
#define SSPP_PIX_EXT_REQ(c)     (0x108 + 0x10 * (c))
#define VIG_OP_MODE             0x200           /* [17] CSC: off for RGB */
#define VIG_SCALE_CONFIG        0x204
#define VIG_PHASE_STEP_X        0x210
#define VIG_PHASE_STEP_Y        0x214
#define VIG_CR_PHASE_STEP_X     0x218
#define VIG_CR_PHASE_STEP_Y     0x21c
#define VIG_INIT_PHASE_X        0x220
#define VIG_INIT_PHASE_Y        0x224
#define OP_MODE_SW_PIX_EXT      (1u << 31)
#define SCALE_XY_BILINEAR       0x00055503u     /* X and Y on; bilinear for every component */

static const struct {
	const char *name;
	uint32_t offset, flush;
} pipes[] = {
	{ "RGB0", 0x15000, 1u << 3 },
	{ "VIG0", 0x05000, 1u << 0 },
	{ "DMA0", 0x25000, 1u << 11 },
};

uint32_t display_bpp = 24;
uint32_t fb_width = PANEL_WIDTH, fb_height = PANEL_HEIGHT;
static const char *pipe_name = "none";
static uint32_t old_format;

static uint32_t SCALE = 3;     /* the font's pixels per pixel: 2 in the scaled mode */
#define CELL_W                  (6 * SCALE)
#define CELL_H                  (13 * SCALE)
#define COLS                    (FB_WIDTH / CELL_W)
#define ROWS                    (FB_HEIGHT / CELL_H)

static uint32_t col, row;

/*
 * Everything written here also goes to memory that survives a reset: the firmware's region for
 * a persistent log (ramoops, 0x9ff00000, 1 MB; mapped uncached by mmu.c so that nothing of it
 * waits in a cache when the reset comes). The next boot shows the end of it: when the phone
 * resets with nothing on the screen to say why, the run before says how far it got.
 */
#define KEEP_BASE       0x9ff00000UL
#define KEEP_MAGIC      0x31414d4950454b52ull       /* "RKEEPMA1" */
#define KEEP_BYTES      (0x100000u - 16)

struct keep {
	uint64_t magic, len;
	char text[KEEP_BYTES];
};

static volatile struct keep *const keep = (volatile struct keep *)KEEP_BASE;
static int keeping, line_start = 1;

static void
keep_char(char c)
{
	if (keeping && keep->len < KEEP_BYTES) {
		keep->text[keep->len] = c;
		keep->len = keep->len + 1;
	}
}

static uint32_t text_colour = 0xffffff;
static void put_raw(char c);

/* The end of what the run before wrote, then a fresh log for this one. */
void
keep_start(void)
{
	if (keep->magic == KEEP_MAGIC && keep->len > 0 && keep->len <= KEEP_BYTES) {
		uint64_t from = keep->len > 1800 ? keep->len - 1800 : 0, hz = READ_SYSREG(cntfrq_el0), t;

		/*
		 * What the kernel said, if it got that far (after the loader's "jumping to"): the lines of
		 * this port's own drivers and the panic, not the whole log the loader prints back.
		 */
		static const char *const wanted[] = { "tissot", "BSD root", "panic(", "Panicked" };
		uint64_t start = 0, line = 0;
		int shown = 0;

		for (uint64_t i = 0; i + 10 < keep->len; i++) {
			if (memcmp((const void *)&keep->text[i], "jumping to", 10) == 0) {
				start = i;
			}
		}
		text_colour = 0xffd040;
		say("--- the run before this one ended with (its kernel's own lines): ---\n");
		for (uint64_t i = start; i < keep->len && shown < 45; i = line) {
			char text[200];
			uint64_t n = 0;

			for (line = i; line < keep->len && keep->text[line] != '\n'; line++) {
				if (n + 1 < sizeof(text)) {
					text[n++] = keep->text[line];
				}
			}
			line++;
			text[n] = 0;
			for (unsigned w = 0; w < sizeof(wanted) / sizeof(wanted[0]); w++) {
				uint64_t len = strlen(wanted[w]);

				for (uint64_t k = 0; k + len <= n; k++) {
					if (memcmp(text + k, wanted[w], len) == 0) {
						for (uint64_t c = 0; c < n; c++) {
							put_raw(text[c]);
						}
						put_raw('\n');
						shown++;
						w = 100;
						break;
					}
				}
			}
		}
		(void)from;
		say("--- (end) ---\n");
		/* time for a photograph */
		for (int left = 30; left > 0; left--) {
			say("%d ", left);
			t = READ_SYSREG(cntpct_el0);
			while (READ_SYSREG(cntpct_el0) - t < hz) {
			}
		}
		say("\n");
		text_colour = 0xffffff;
	}
	keep->magic = KEEP_MAGIC;
	keep->len = 0;
	keeping = 1;
}

static inline uint32_t
mmio_rd(uint64_t a)
{
	return *(volatile uint32_t *)a;
}

static inline void
mmio_wr(uint64_t a, uint32_t v)
{
	*(volatile uint32_t *)a = v;
}

static void
pixel(uint32_t x, uint32_t y, uint32_t rgb)
{
	if (display_bpp == 32) {
		((volatile uint32_t *)FB_BASE)[y * FB_WIDTH + x] = rgb;
	} else {
		volatile uint8_t *p = (volatile uint8_t *)FB_BASE + (y * FB_WIDTH + x) * 3;
		p[0] = (uint8_t)rgb;
		p[1] = (uint8_t)(rgb >> 8);
		p[2] = (uint8_t)(rgb >> 16);
	}
}

/* VIG0 reading the same memory at fb_width x fb_height, scaled up to the panel in place of `rgb` */
static void
scale_with_vig0(uint64_t rgb, uint32_t rgb_flush)
{
	uint32_t w = fb_width, h = fb_height, layer;
	/* 2^21 is one source pixel per panel pixel (Linux: calc_phase_step()) */
	uint32_t step_x = (uint32_t)(((uint64_t)w << 21) / PANEL_WIDTH), step_y = (uint32_t)(((uint64_t)h << 21) / PANEL_HEIGHT);

	mmio_wr(VIG0 + SSPP_SRC0_ADDR, (uint32_t)FB_BASE);
	mmio_wr(VIG0 + SSPP_SRC_YSTRIDE, w * 4);
	mmio_wr(VIG0 + SSPP_SRC_IMG_SIZE, h << 16 | w);
	mmio_wr(VIG0 + SSPP_SRC_SIZE, h << 16 | w);
	mmio_wr(VIG0 + SSPP_SRC_XY, 0);
	mmio_wr(VIG0 + SSPP_OUT_SIZE, PANEL_HEIGHT << 16 | PANEL_WIDTH);
	mmio_wr(VIG0 + SSPP_OUT_XY, 0);
	mmio_wr(VIG0 + SSPP_SRC_FORMAT, FORMAT_XRGB8888);
	mmio_wr(VIG0 + SSPP_SRC_UNPACK, UNPACK_XRGB8888);
	mmio_wr(VIG0 + SSPP_SRC_ADDR_SW_STATUS, 0);
	mmio_wr(VIG0 + SSPP_DECIMATION, 0);
	for (int i = 0; i < 3; i++) {
		mmio_wr(VIG0 + SSPP_REQPRIO_WM(i), mmio_rd(rgb + SSPP_REQPRIO_WM(i)));
		/* bilinear upscaling reads one pixel past the right and bottom edges: repeat the last */
		mmio_wr(VIG0 + SSPP_PIX_EXT_LR(i), 1u << 16);
		mmio_wr(VIG0 + SSPP_PIX_EXT_TB(i), 1u << 16);
		mmio_wr(VIG0 + SSPP_PIX_EXT_REQ(i), (h + 1) << 16 | (w + 1));
	}
	mmio_wr(VIG0 + VIG_OP_MODE, 0);
	mmio_wr(VIG0 + VIG_PHASE_STEP_X, step_x);
	mmio_wr(VIG0 + VIG_PHASE_STEP_Y, step_y);
	mmio_wr(VIG0 + VIG_CR_PHASE_STEP_X, step_x);
	mmio_wr(VIG0 + VIG_CR_PHASE_STEP_Y, step_y);
	mmio_wr(VIG0 + VIG_INIT_PHASE_X, 0);
	mmio_wr(VIG0 + VIG_INIT_PHASE_Y, 0);
	mmio_wr(VIG0 + VIG_SCALE_CONFIG, SCALE_XY_BILINEAR);
	mmio_wr(VIG0 + SSPP_SRC_OP_MODE, OP_MODE_SW_PIX_EXT);
	/* the mixer's stage of the RGB pipe (CTL_LAYER: VIG0 [2:0], RGB0 [11:9]) to VIG0 */
	layer = mmio_rd(CTL0_LAYER0);
	mmio_wr(CTL0_LAYER0, (layer & ~(7u << 9 | 7u)) | ((layer >> 9) & 7));
	mmio_wr(MDP_CTL0_FLUSH, FLUSH_VIG0 | rgb_flush | FLUSH_LM0 | FLUSH_CTL);
	say("Mi A1 shim: drawn at %ux%u, scaled to %ux%u by VIG0 (mixer layer %x -> %x, phase steps %x %x)\n", w, h,
	    PANEL_WIDTH, PANEL_HEIGHT, layer, mmio_rd(CTL0_LAYER0), step_x, step_y);
}

/* A picture of width x height scaled to the panel (up, at most 3 times), or 0, 0: the panel's own size */
static int
scaled_size(uint32_t width, uint32_t height)
{
	if (width < PANEL_WIDTH / 3 || width >= PANEL_WIDTH || height < PANEL_HEIGHT / 3 || height > 2 * PANEL_HEIGHT) {
		return 0;
	}
	fb_width = width & ~1u;
	fb_height = height & ~1u;
	SCALE = 2;
	return 1;
}

void
display_init(uint32_t width, uint32_t height)
{
#ifdef SHIM_UART
	/* QEMU (qemu-shim.sh) has no MDP: with fbw/fbh a picture in memory alone, read back with pmemsave */
	if (scaled_size(width, height)) {
		display_bpp = 32;
		memset((void *)FB_BASE, 0, PANEL_WIDTH * PANEL_HEIGHT * 4);
		pipe_name = "none (QEMU: memory only)";
	}
#endif
	for (unsigned i = 0; i < sizeof(pipes) / sizeof(pipes[0]); i++) {
		uint64_t base = MDP_BASE + pipes[i].offset;

		if (display_bpp == 32 || mmio_rd(base + SSPP_SRC0_ADDR) != (uint32_t)FB_BASE) {
			continue;
		}
		old_format = mmio_rd(base + SSPP_SRC_FORMAT);
		/* black in the new layout first, then the switch: one frame of the old reading of it at most */
		memset((void *)FB_BASE, 0, PANEL_WIDTH * PANEL_HEIGHT * 4);
		mmio_wr(base + SSPP_SRC_FORMAT, FORMAT_XRGB8888);
		mmio_wr(base + SSPP_SRC_UNPACK, UNPACK_XRGB8888);
		mmio_wr(base + SSPP_SRC_YSTRIDE, PANEL_WIDTH * 4);
		mmio_wr(MDP_CTL0_FLUSH, pipes[i].flush | FLUSH_LM0 | FLUSH_CTL);
		display_bpp = 32;
		pipe_name = pipes[i].name;
		if (base != VIG0 && scaled_size(width, height)) {
			scale_with_vig0(base, pipes[i].flush);
		}
		break;
	}
	if (display_bpp != 32) {
		memset((void *)FB_BASE, 0, PANEL_WIDTH * PANEL_HEIGHT * 3);
	}
	say("Mi A1 shim: display %ux%u, %u bits a pixel (pipe %s, format was %x)\n", FB_WIDTH, FB_HEIGHT, display_bpp,
	    pipe_name, old_format);
}

static void
glyph(uint32_t cx, uint32_t cy, char c, uint32_t rgb)
{
	const unsigned *g;

	if (c < 32 || c > 127) {
		c = '?';
	}
	g = &font5x12[(c - 32) * 2];
	for (uint32_t y = 0; y < 12; y++) {
		uint32_t bits = y < 6 ? g[0] >> (y * 5) : g[1] >> ((y - 6) * 5);

		for (uint32_t x = 0; x < 5; x++) {
			uint32_t on = (bits >> x) & 1 ? rgb : 0;

			for (uint32_t sy = 0; sy < SCALE; sy++) {
				for (uint32_t sx = 0; sx < SCALE; sx++) {
					pixel(cx * CELL_W + x * SCALE + sx, cy * CELL_H + y * SCALE + sy, on);
				}
			}
		}
	}
}

static void
newline(void)
{
	uint32_t bytes = display_bpp / 8;

	col = 0;
	if (++row < ROWS) {
		return;
	}
	row = ROWS - 1;
	memmove((void *)FB_BASE, (void *)(FB_BASE + (uint64_t)CELL_H * FB_WIDTH * bytes),
	    (uint64_t)(ROWS - 1) * CELL_H * FB_WIDTH * bytes);
	memset((void *)(FB_BASE + (uint64_t)(ROWS - 1) * CELL_H * FB_WIDTH * bytes), 0,
	    (uint64_t)CELL_H * FB_WIDTH * bytes);
}

void
console_putc(char c)
{
	keep_char(c);
	if (line_start && c != '\n' && keeping) {
		uint64_t t = READ_SYSREG(cntpct_el0), hz = READ_SYSREG(cntfrq_el0);
		char stamp[24];

		line_start = 0;
		snprintf(stamp, sizeof(stamp), "%llu.%03llu ", (unsigned long long)(t / hz),
		    (unsigned long long)(t % hz * 1000 / hz));
		for (char *p = stamp; *p; p++) {
			keep_char(*p);
			put_raw(*p);
		}
	}
	if (c == '\n') {
		line_start = 1;
	}
	put_raw(c);
}

static void
put_raw(char c)
{
#ifdef SHIM_UART
	/* testing under QEMU: a PL011 as well */
	if (c == '\n') {
		*(volatile uint32_t *)SHIM_UART = '\r';
	}
	*(volatile uint32_t *)SHIM_UART = (uint8_t)c;
#endif
	if (c == '\n') {
		newline();
		return;
	}
	if (c == '\r') {
		col = 0;
		return;
	}
	if (col >= COLS) {
		newline();
	}
	glyph(col++, row, c, text_colour);
}

static void
vsay(const char *fmt, va_list ap)
{
	char line[512];

	vsnprintf(line, sizeof(line), fmt, ap);
	for (const char *p = line; *p; p++) {
		console_putc(*p);
	}
}

void
say(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsay(fmt, ap);
	va_end(ap);
}

void
stop(const char *fmt, ...)
{
	va_list ap;

	text_colour = 0xff4040;
	va_start(ap, fmt);
	vsay(fmt, ap);
	va_end(ap);
	say("\nStopped. Hold power for 10 s to restart.\n");
	for (;;) {
		__asm__ volatile("wfi");
	}
}

/* from the vectors (start.S): what went wrong, where */
void
shim_exception(uint64_t kind, const uint64_t *regs)
{
	text_colour = 0xff4040;
	say("\nEXCEPTION %llu: ESR %llx ELR %llx FAR %llx SPSR %llx\n", kind, READ_SYSREG(esr_el1), READ_SYSREG(elr_el1),
	    READ_SYSREG(far_el1), READ_SYSREG(spsr_el1));
	for (int i = 0; i < 31; i += 3) {
		say("x%d %llx x%d %llx x%d %llx\n", i, regs[i], i + 1, i + 1 < 31 ? regs[i + 1] : 0ull, i + 2,
		    i + 2 < 31 ? regs[i + 2] : 0ull);
	}
	stop("in the shim or the loader");
}
