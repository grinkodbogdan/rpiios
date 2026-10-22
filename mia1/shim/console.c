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
#define FLUSH_LM0               (1u << 6)
#define FLUSH_CTL               (1u << 17)

static const struct {
	const char *name;
	uint32_t offset, flush;
} pipes[] = {
	{ "RGB0", 0x15000, 1u << 3 },
	{ "VIG0", 0x05000, 1u << 0 },
	{ "DMA0", 0x25000, 1u << 11 },
};

uint32_t display_bpp = 24;
static const char *pipe_name = "none";
static uint32_t old_format;

#define SCALE                   3
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
		uint64_t from = keep->len > 1500 ? keep->len - 1500 : 0;

		text_colour = 0xffd040;
		say("--- the run before this one ended with: ---\n");
		for (uint64_t i = from; i < keep->len; i++) {
			put_raw(keep->text[i]);
		}
		say("\n--- (end) ---\n");
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

void
display_init(void)
{
	for (unsigned i = 0; i < sizeof(pipes) / sizeof(pipes[0]); i++) {
		uint64_t base = MDP_BASE + pipes[i].offset;

		if (mmio_rd(base + SSPP_SRC0_ADDR) != (uint32_t)FB_BASE) {
			continue;
		}
		old_format = mmio_rd(base + SSPP_SRC_FORMAT);
		/* black in the new layout first, then the switch: one frame of the old reading of it at most */
		memset((void *)FB_BASE, 0, FB_WIDTH * FB_HEIGHT * 4);
		mmio_wr(base + SSPP_SRC_FORMAT, FORMAT_XRGB8888);
		mmio_wr(base + SSPP_SRC_UNPACK, UNPACK_XRGB8888);
		mmio_wr(base + SSPP_SRC_YSTRIDE, FB_WIDTH * 4);
		mmio_wr(MDP_CTL0_FLUSH, pipes[i].flush | FLUSH_LM0 | FLUSH_CTL);
		display_bpp = 32;
		pipe_name = pipes[i].name;
		break;
	}
	if (display_bpp != 32) {
		memset((void *)FB_BASE, 0, FB_WIDTH * FB_HEIGHT * 3);
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
