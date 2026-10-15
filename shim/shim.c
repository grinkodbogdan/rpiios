/*
 * Raspberry Pi 3 boot shim for the iOS 7 XNU kernel.
 *
 * Does for the Pi what the HD2's MAGLDR initrd prefix does for the HD2:
 *   1. asks the VideoCore firmware (mailbox property channel) for the board's
 *      RAM size, the SD controller clock, the display size and a 480x800
 *      16bpp framebuffer, scaled by the GPU to the HDMI mode;
 *   2. copies the Mach-O kernel's segments to physical 16 MiB + offset;
 *   3. fills the device tree's /chosen/rpi3-handoff and builds boot_args;
 *   4. clears the memory XNU uses for its first page tables and jumps in.
 *
 * Runs with the MMU off, so every access is strongly ordered: the code is
 * built without unaligned accesses and uses only aligned word copies for
 * large moves.
 */
#include <stdint.h>
#include <stddef.h>
#include "payload.h"

#define PERIPHERAL_BASE   0x3f000000u
#define MBOX_BASE         (PERIPHERAL_BASE + 0xb880u)
#define MBOX_READ         (MBOX_BASE + 0x00u)
#define MBOX_STATUS       (MBOX_BASE + 0x18u)
#define MBOX_WRITE        (MBOX_BASE + 0x20u)
#define MBOX_WRITE_STATUS (MBOX_BASE + 0x38u)
#define MBOX_FULL         0x80000000u
#define MBOX_EMPTY        0x40000000u
#define MBOX_CH_PROP      8u

#define UART0_DR          (PERIPHERAL_BASE + 0x201000u)
#define UART0_FR          (PERIPHERAL_BASE + 0x201018u)

#define PAYLOAD_OFFSET    0x10000u        /* shim image is padded to 64 KiB */
#define PHYS_BASE         0x01000000u     /* kernel's physBase (16 MiB) */
#define VIRT_BASE         0x80000000u
#define EARLY_VM_RESERVE  0x00300000u     /* XNU's first page tables live here */
/* Memory handed to XNU by default: the 444 MiB the HD2 kernel runs with. Its
 * boot-time pmap bookkeeping is sized for that; "rpi3_mem_mb=N" overrides. */
#define DEFAULT_MEM_BYTES 0x1bc00000u

#define FB_WIDTH          480u
#define FB_HEIGHT         800u
#define FB_DEPTH          16u
#define FB_PITCH          960u
#define FB_BYTES          (FB_PITCH * FB_HEIGHT)
#define MACHINE_TYPE      3138u

/* --- boot_args, exactly as pexpert/pexpert/arm/boot.h ------------------ */
#define BOOT_LINE_LENGTH 256
struct boot_video {
    uint32_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};
struct boot_args {
    uint16_t Revision, Version;
    uint32_t virtBase, physBase, memSize, topOfKernelData;
    struct boot_video Video;
    uint32_t machineType;
    uint32_t deviceTreeP;
    uint32_t deviceTreeLength;
    char CommandLine[BOOT_LINE_LENGTH];
};
_Static_assert(sizeof(struct boot_args) == 312, "boot_args layout");

/* --- handoff, exactly as pexpert/arm/rpi3_board.h ----------------------- */
struct rpi3_handoff {
    uint32_t magic, version, bytes, flags;
    uint32_t entry_cpsr, entry_sctlr;
    uint32_t board_revision, arm_memory_bytes;
    uint32_t fb_physical, fb_bytes, fb_pitch, fb_width, fb_height, fb_depth;
    uint32_t display_width, display_height;
    uint32_t memory_base, memory_bytes, top_of_kernel_data;
    uint32_t shim_start, shim_end, payload_start, payload_end;
    uint32_t emmc_clock_hz;
    uint32_t arm_clock_hz;       /* measured after the shim's request */
    uint32_t reserved[7];
};
_Static_assert(sizeof(struct rpi3_handoff) == 128, "handoff layout");
#define HANDOFF_MAGIC   0x33495052u
#define HANDOFF_VERSION 1u

extern void enter_kernel(uint32_t entry, uint32_t boot_args) __attribute__((noreturn));
extern char __image_end[], __stack_top[];

static inline void w32(uint32_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline uint32_t r32(uint32_t a) { return *(volatile uint32_t *)a; }
static inline void dsb(void) { __asm__ volatile("dsb" ::: "memory"); }

static int uart_on;

static void putc_(char c)
{
    unsigned spins;
    if (!uart_on) return;
    if (c == '\n') putc_('\r');
    for (spins = 0; spins < 100000u && (r32(UART0_FR) & (1u << 5)); spins++) {}
    w32(UART0_DR, (uint8_t)c);
}

static void puts_(const char *s) { while (*s) putc_(*s++); }

static void puthex(uint32_t v)
{
    int i;
    puts_("0x");
    for (i = 28; i >= 0; i -= 4) putc_("0123456789abcdef"[(v >> i) & 15]);
}

static void say(const char *what, uint32_t v) { puts_("rpiios shim: "); puts_(what); puts_(" "); puthex(v); puts_("\n"); }

static void *memset_(void *d, int c, size_t n)
{
    uint8_t *p = d;
    while (n && ((uintptr_t)p & 3)) { *p++ = (uint8_t)c; n--; }
    {
        uint32_t w = (uint8_t)c * 0x01010101u;
        uint32_t *q = (uint32_t *)p;
        for (; n >= 4; n -= 4) *q++ = w;
        p = (uint8_t *)q;
    }
    while (n--) *p++ = (uint8_t)c;
    return d;
}

static void copy_(void *d, const void *s, size_t n)
{
    uint8_t *dp = d;
    const uint8_t *sp = s;
    if ((((uintptr_t)dp | (uintptr_t)sp) & 3) == 0) {
        uint32_t *dw = (uint32_t *)dp;
        const uint32_t *sw = (const uint32_t *)sp;
        for (; n >= 4; n -= 4) *dw++ = *sw++;
        dp = (uint8_t *)dw; sp = (const uint8_t *)sw;
    }
    while (n--) *dp++ = *sp++;
}

/* The compiler may emit calls to these for struct copies/clears. */
void *memset(void *d, int c, size_t n) { return memset_(d, c, n); }
void *memcpy(void *d, const void *s, size_t n) { copy_(d, s, n); return d; }

static __attribute__((noreturn)) void fail(const char *why, uint32_t v)
{
    uart_on = 1;   /* a failure is always worth trying to print */
    say(why, v);
    for (;;) __asm__ volatile("wfi");
}

/* --- Mailbox property interface ---------------------------------------- */

static volatile uint32_t mbox[64] __attribute__((aligned(16)));

/* Send the request in mbox[] and wait for the firmware's answer.
 * Bus addresses for ARM RAM use the uncached 0xC0000000 alias on Pi 2/3. */
static int mbox_call(void)
{
    uint32_t msg = ((uint32_t)(uintptr_t)mbox | 0xc0000000u) | MBOX_CH_PROP;
    unsigned spins;
    dsb();
    for (spins = 0; r32(MBOX_WRITE_STATUS) & MBOX_FULL; spins++)
        if (spins > 10000000u) return 0;
    w32(MBOX_WRITE, msg);
    for (spins = 0; spins < 50000000u; spins++) {
        if (r32(MBOX_STATUS) & MBOX_EMPTY) continue;
        if (r32(MBOX_READ) == msg) { dsb(); return mbox[1] == 0x80000000u; }
    }
    return 0;
}

/* One-tag request; returns the response's first value words in out[]. */
static int mbox_tag(uint32_t tag, const uint32_t *in, unsigned in_words,
                    uint32_t *out, unsigned value_words)
{
    unsigned i;
    mbox[0] = (6 + value_words) * 4;
    mbox[1] = 0;
    mbox[2] = tag;
    mbox[3] = value_words * 4;
    mbox[4] = 0;
    for (i = 0; i < value_words; i++) mbox[5 + i] = i < in_words ? in[i] : 0;
    mbox[5 + value_words] = 0;
    if (!mbox_call() || !(mbox[4] & 0x80000000u)) return 0;
    for (i = 0; out && i < value_words; i++) out[i] = mbox[5 + i];
    return 1;
}

/* The framebuffer has to be configured in a single property message. */
static int framebuffer_alloc(uint32_t *base, uint32_t *pitch, uint32_t display_w, uint32_t display_h)
{
    unsigned n = 2;
    unsigned at_alloc, at_pitch;
    uint32_t left = 0;
    /* Pillarbox: keep 3:5 by narrowing the area the GPU scales into. */
    if (display_w && display_h && display_h * 3 / 5 < display_w)
        left = (display_w - display_h * 3 / 5) / 2;
#define TAG(t, words) do { mbox[n++] = (t); mbox[n++] = (words) * 4; mbox[n++] = 0; } while (0)
    TAG(0x00048003, 2); mbox[n++] = FB_WIDTH; mbox[n++] = FB_HEIGHT;    /* physical */
    TAG(0x00048004, 2); mbox[n++] = FB_WIDTH; mbox[n++] = FB_HEIGHT;    /* virtual */
    TAG(0x00048009, 2); mbox[n++] = 0; mbox[n++] = 0;                   /* offset */
    TAG(0x00048005, 1); mbox[n++] = FB_DEPTH;                           /* depth */
    TAG(0x0004800a, 4); mbox[n++] = 0; mbox[n++] = 0;                   /* overscan */
                        mbox[n++] = left; mbox[n++] = left;
    TAG(0x00040001, 2); at_alloc = n; mbox[n++] = 4096; mbox[n++] = 0;  /* allocate */
    TAG(0x00040008, 1); at_pitch = n; mbox[n++] = 0;                    /* pitch */
    mbox[n++] = 0;
#undef TAG
    mbox[0] = n * 4;
    mbox[1] = 0;
    if (!mbox_call()) return 0;
    *base = mbox[at_alloc] & 0x3fffffffu;   /* VideoCore bus -> ARM physical */
    *pitch = mbox[at_pitch];
    return mbox[at_alloc] != 0;
}

/* --- Command line from the firmware (cmdline.txt) ----------------------- */

static uint32_t be32(const uint8_t *p)
{ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* Flattened device tree: /chosen/bootargs. Minimal walk, no allocation. */
static const char *fdt_bootargs(uint32_t addr)
{
    const uint8_t *fdt = (const uint8_t *)(uintptr_t)addr;
    const uint8_t *st, *strings;
    uint32_t off_struct, off_strings, depth = 0, in_chosen = 0;
    if (addr == 0 || addr >= PHYS_BASE || be32(fdt) != 0xd00dfeedu) return 0;
    off_struct = be32(fdt + 8);
    off_strings = be32(fdt + 12);
    st = fdt + off_struct;
    strings = fdt + off_strings;
    for (;;) {
        uint32_t token = be32(st);
        st += 4;
        if (token == 1) {                        /* BEGIN_NODE */
            const char *name = (const char *)st;
            size_t len = 0;
            while (name[len]) len++;
            depth++;
            in_chosen = (depth == 2 && streq(name, "chosen"));
            st += (len + 4) & ~3u;
        } else if (token == 2) {                 /* END_NODE */
            if (depth == 2) in_chosen = 0;
            if (depth-- == 0) return 0;
        } else if (token == 3) {                 /* PROP */
            uint32_t len = be32(st), nameoff = be32(st + 4);
            const char *value = (const char *)(st + 8);
            if (in_chosen && streq((const char *)strings + nameoff, "bootargs")) return value;
            st += 8 + ((len + 3) & ~3u);
        } else if (token == 4) {                 /* NOP */
        } else return 0;                          /* END or garbage */
    }
}

/* ATAG list: ATAG_CMDLINE. */
static const char *atag_cmdline(uint32_t addr)
{
    const uint32_t *t = (const uint32_t *)(uintptr_t)addr;
    unsigned guard;
    if (addr == 0 || addr >= PHYS_BASE || (addr & 3) || t[1] != 0x54410001u) return 0;
    for (guard = 0; guard < 64 && t[0]; guard++) {
        if (t[1] == 0x54410009u) return (const char *)&t[2];
        t += t[0];
    }
    return 0;
}

/* Value of "key=decimal" in s, or 0. */
static uint32_t word_value(const char *s, const char *key)
{
    size_t n = 0, i;
    if (!s) return 0;
    while (key[n]) n++;
    for (i = 0; s[i]; i++) {
        size_t k = 0;
        uint32_t v = 0;
        if (i && s[i - 1] != ' ') continue;
        while (k < n && s[i + k] == key[k]) k++;
        if (k != n || s[i + n] != '=') continue;
        for (k = i + n + 1; s[k] >= '0' && s[k] <= '9'; k++) v = v * 10 + (uint32_t)(s[k] - '0');
        return v;
    }
    return 0;
}

static int has_word(const char *s, const char *word)
{
    size_t n = 0, i;
    while (word[n]) n++;
    for (i = 0; s[i]; i++) {
        size_t k = 0;
        if (i && s[i - 1] != ' ') continue;
        while (k < n && s[i + k] == word[k]) k++;
        if (k == n && (s[i + n] == ' ' || s[i + n] == 0)) return 1;
    }
    return 0;
}

/* --- Main -------------------------------------------------------------- */

void shim_main(uint32_t load_addr, uint32_t firmware_r2, uint32_t entry_cpsr, uint32_t entry_sctlr)
{
    const struct rpi3_payload *p = (const struct rpi3_payload *)(uintptr_t)(load_addr + PAYLOAD_OFFSET);
    const uint8_t *pbase = (const uint8_t *)p;
    struct boot_args *args;
    struct rpi3_handoff *h;
    uint32_t v[4], i, kernel_end, cursor, top, dt_phys;
    uint32_t revision = 0, arm_base = 0, arm_bytes = 0, emmc_hz = 0;
    uint32_t disp_w = 0, disp_h = 0, fb = 0, pitch = 0, mem_bytes, arm_hz = 0;
    const char *extra;

    extra = fdt_bootargs(firmware_r2);
    if (!extra) extra = atag_cmdline(firmware_r2);
    uart_on = has_word(p->cmdline, "rpi3_uart=1") || (extra && has_word(extra, "rpi3_uart=1"));
    puts_("\nrpiios shim: Raspberry Pi 3 -> XNU\n");
    say("loaded at", load_addr);

    if (p->magic[0] != PAYLOAD_MAGIC0 || p->magic[1] != PAYLOAD_MAGIC1 ||
        p->version != PAYLOAD_VERSION || p->segment_count == 0 ||
        p->segment_count > PAYLOAD_MAX_SEGMENTS)
        fail("bad payload header at", (uint32_t)(uintptr_t)p);

    if (mbox_tag(0x00010002, 0, 0, v, 1)) revision = v[0];
    if (!mbox_tag(0x00010005, 0, 0, v, 2)) fail("mailbox: no ARM memory size", 0);
    arm_base = v[0]; arm_bytes = v[1];
    { uint32_t id = 1; if (mbox_tag(0x00030002, &id, 1, v, 2)) emmc_hz = v[1]; }
    if (mbox_tag(0x00040003, 0, 0, v, 2)) { disp_w = v[0]; disp_h = v[1]; }
    {   /* Power the USB host controller (device 3: on, wait until stable). */
        uint32_t in[2] = {3, 3};
        if (mbox_tag(0x00028001, in, 2, v, 2)) say("USB power state", v[1]);
    }
    {   /*
         * The firmware starts the ARM cores at their minimum clock (600 MHz
         * on a 3B+) and leaves raising it to the OS's cpufreq driver. Ask
         * for the maximum (clock 3 = ARM); the firmware still throttles on
         * heat or low voltage. rpi3_armclk=0 on the command line skips it.
         */
        uint32_t in[3] = {3, 0, 0};
        int skip = has_word(p->cmdline, "rpi3_armclk=0") || (extra && has_word(extra, "rpi3_armclk=0"));
        if (!skip && mbox_tag(0x00030004, in, 1, v, 2) && v[1]) {
            in[1] = v[1];
            mbox_tag(0x00038002, in, 3, v, 2);
        }
        if (mbox_tag(0x00030047, in, 1, v, 2) && v[1]) arm_hz = v[1];
        else if (mbox_tag(0x00030002, in, 1, v, 2)) arm_hz = v[1];
        say("ARM clock Hz", arm_hz);
    }
    say("board revision", revision);
    say("ARM memory bytes", arm_bytes);
    say("EMMC clock Hz", emmc_hz);
    say("display", (disp_w << 16) | disp_h);
    if (arm_base != 0 || arm_bytes < PHYS_BASE + 0x04000000u) fail("unexpected ARM memory", arm_bytes);

    if (!framebuffer_alloc(&fb, &pitch, disp_w, disp_h)) fail("framebuffer allocation failed", 0);
    say("framebuffer", fb);
    if (pitch != FB_PITCH) fail("framebuffer pitch", pitch);
    if (fb < arm_bytes || fb + FB_BYTES > PERIPHERAL_BASE) fail("framebuffer outside VideoCore memory", fb);
    memset_((void *)(uintptr_t)fb, 0, FB_BYTES);

    /* Kernel segments: payload-relative file data, zero-filled to vmsize. */
    kernel_end = 0;
    for (i = 0; i < p->segment_count; i++) {
        const struct rpi3_payload_segment *s = &p->segments[i];
        uint32_t dst;
        if (!s->vmsize) continue;
        if (s->vmaddr < VIRT_BASE + 0x1000u || s->filesize > s->vmsize)
            fail("bad segment", s->vmaddr);
        dst = s->vmaddr - VIRT_BASE + PHYS_BASE;
        copy_((void *)(uintptr_t)dst, pbase + p->kernel_offset + s->fileoff, s->filesize);
        memset_((void *)(uintptr_t)(dst + s->filesize), 0, s->vmsize - s->filesize);
        if (dst + s->vmsize > kernel_end) kernel_end = dst + s->vmsize;
    }
    say("kernel end", kernel_end);

    /* Device tree, then boot_args, then a 16 KiB-aligned top of kernel data. */
    cursor = (kernel_end + 0xfffu) & ~0xfffu;
    dt_phys = cursor;
    copy_((void *)(uintptr_t)dt_phys, pbase + p->dt_offset, p->dt_bytes);
    cursor = (dt_phys + p->dt_bytes + 0xfffu) & ~0xfffu;
    args = (struct boot_args *)(uintptr_t)cursor;
    cursor += 0x1000u;
    top = (cursor + 0x3fffu) & ~0x3fffu;

    mem_bytes = (arm_bytes - PHYS_BASE) & ~0xfffffu;
    {
        uint32_t mb = word_value(extra, "rpi3_mem_mb");
        if (!mb) mb = word_value(p->cmdline, "rpi3_mem_mb");
        uint32_t want = mb ? mb << 20 : DEFAULT_MEM_BYTES;
        if (want < mem_bytes) mem_bytes = want;
    }
    say("memory for XNU", mem_bytes);
    if (top + EARLY_VM_RESERVE > PHYS_BASE + mem_bytes) fail("not enough RAM above the kernel", top);
    memset_((void *)(uintptr_t)top, 0, EARLY_VM_RESERVE);

    h = (struct rpi3_handoff *)(uintptr_t)(dt_phys + p->dt_handoff_offset);
    memset_(h, 0, sizeof(*h));
    h->magic = HANDOFF_MAGIC;
    h->version = HANDOFF_VERSION;
    h->bytes = sizeof(*h);
    h->entry_cpsr = entry_cpsr;
    h->entry_sctlr = entry_sctlr;
    h->board_revision = revision;
    h->arm_memory_bytes = arm_bytes;
    h->fb_physical = fb; h->fb_bytes = FB_BYTES; h->fb_pitch = pitch;
    h->fb_width = FB_WIDTH; h->fb_height = FB_HEIGHT; h->fb_depth = FB_DEPTH;
    h->display_width = disp_w; h->display_height = disp_h;
    h->memory_base = PHYS_BASE; h->memory_bytes = mem_bytes; h->top_of_kernel_data = top;
    h->shim_start = 0x00f00000u; h->shim_end = (uint32_t)(uintptr_t)__stack_top;
    h->payload_start = (uint32_t)(uintptr_t)p; h->payload_end = (uint32_t)(uintptr_t)p + p->total_bytes;
    h->emmc_clock_hz = emmc_hz;
    h->arm_clock_hz = arm_hz;

    memset_(args, 0, sizeof(*args));
    args->Revision = 1;
    args->Version = 2;
    args->virtBase = VIRT_BASE;
    args->physBase = PHYS_BASE;
    args->memSize = mem_bytes;
    args->topOfKernelData = top;
    args->Video.v_baseAddr = fb;
    args->Video.v_display = 1;
    args->Video.v_rowBytes = pitch;
    args->Video.v_width = FB_WIDTH;
    args->Video.v_height = FB_HEIGHT;
    args->Video.v_depth = FB_DEPTH;
    args->machineType = MACHINE_TYPE;
    args->deviceTreeP = dt_phys - PHYS_BASE + VIRT_BASE;   /* kernel virtual */
    args->deviceTreeLength = p->dt_bytes;
    {
        /* Built-in arguments first, then the user's cmdline.txt. */
        size_t n = 0;
        const char *s;
        for (s = p->cmdline; *s && n < BOOT_LINE_LENGTH - 1; s++) args->CommandLine[n++] = *s;
        if (extra) {
            if (n && n < BOOT_LINE_LENGTH - 1) args->CommandLine[n++] = ' ';
            for (s = extra; *s && n < BOOT_LINE_LENGTH - 1; s++) args->CommandLine[n++] = *s;
        }
        args->CommandLine[n] = 0;
        puts_("rpiios shim: boot-args \""); puts_(args->CommandLine); puts_("\"\n");
    }

    say("top of kernel data", top);
    say("entering kernel at", p->entry_va - VIRT_BASE + PHYS_BASE);
    enter_kernel(p->entry_va - VIRT_BASE + PHYS_BASE, (uint32_t)(uintptr_t)args);
}
