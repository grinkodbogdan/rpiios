/*
 * Xiaomi Mi A1 (tissot) boot shim: from aboot to HackiOS's XNU loader.
 *
 *   fastboot boot mia1-ios.img
 *
 * The boot image (mia1/build.sh) is an Android boot image whose kernel is this
 * shim and whose ramdisk is a "MIA1BOOT" container: the XNU loader (loader.efi)
 * and its payload (the kernel, root.apfs and the boot-args: mkpayload.py). The
 * iOS system itself is on the phone's userdata partition, read by the kernel's
 * eMMC driver (emmc_root=userdata): nothing here touches the storage.
 *
 * In order:
 *   1. the memory banks and the ramdisk, from the device tree aboot passed
 *   2. the MMU and caches on (mmu.c)
 *   3. the display at 32 bits a pixel, and text on it (console.c)
 *   4. the memory map: the banks less the firmware's regions, this shim, the
 *      ramdisk; the payload copied to the top of the largest free block
 *   5. the loader run under the UEFI of efi.c; it starts the kernel
 * Every step says what it does on the screen: a photograph of it is the log.
 */
#include "shim.h"
#include "efi.h"

#define PAYLOAD_SPACE   0x04000000ull           /* the loader's PAYLOAD_MAX_SIZE: payload, then its panic log */
#define MB2             0x200000ull

extern char _head[], _image_end[];

/* what the firmware keeps for itself (msm8953.dtsi and msm8953-xiaomi-tissot.dts, reserved-memory) */
static const struct region firmware[] = {
	{ 0x84a00000, 0x0d700000, 0 /* reserved */, "TrustZone apps, shared memory, modem, DSPs, display, video" },
	{ 0x9ff00000, 0x00100000, 0, "persistent log" },
	/*
	 * Not in the published reserved-memory lists, but the phone resets (no fault, a reset a
	 * moment later: an access the secure firmware does not allow) soon after the payload was
	 * copied to 0x80a00000. Left alone until mia1-memprobe.img says what is there.
	 */
	{ 0x80000000, 0x04a00000, 0, "protected? (resets the phone when written)" },
};

/*
 * The application processor's watchdog (APSS WDOG, 0x0b017000): aboot enables it, bite after 10 s
 * (LK platform/msm_shared/wdog.c, msm_wdog_init), and leaves the feeding to the kernel it starts
 * (Linux's msm_watchdog). Nothing here or in XNU feeds it: switched off. LK's msm8952 headers say
 * the enable bit is at +0x04 and the pet at +0x08, Linux's driver the other way round: 0 to both
 * disables it either way, and the bite time goes to its maximum besides.
 */
#define WDOG_BASE       0x0b017000UL

/*
 * What to do about it is being found out on the phone (WDT_MODE, set by build.sh for the test
 * images): 0 leave it alone, 8 write 0 to +0x08 (Linux's WDT_EN), 4 write 0 to +0x04 (LK msm8952's
 * CTL). Each test image counts seconds on the screen before going on, so a photo shows when (and
 * whether) the phone resets.
 */
#ifndef WDT_MODE
#define WDT_MODE 0
#endif
#ifndef WDT_WATCH_S
#define WDT_WATCH_S 0
#endif

static void
watchdog_off(void)
{
#if WDT_MODE == 8 || WDT_MODE == 4
	*(volatile uint32_t *)(WDOG_BASE + WDT_MODE) = 0;
#endif
}

static void
watchdog_watch(void)
{
	uint64_t start = READ_SYSREG(cntpct_el0), hz = READ_SYSREG(cntfrq_el0);

	if (WDT_WATCH_S == 0) {
		return;
	}
	say("watchdog test, mode %d: seconds since the shim started (and since power on):\n", WDT_MODE);
	for (uint64_t s = 0; s <= WDT_WATCH_S; s++) {
		while (READ_SYSREG(cntpct_el0) - start < s * hz) {
		}
		say(" %llu(%llu)", (unsigned long long)s, (unsigned long long)(READ_SYSREG(cntpct_el0) / hz));
	}
	say("\nstill here after %d s: going on\n", WDT_WATCH_S);
}

#ifdef MEMPROBE
/*
 * mia1-memprobe.img: every free 16 MB of RAM read and written back, one word in every 64 KB,
 * with a pause after each, its address on the screen and in the persistent log first: when an
 * access the secure firmware does not allow resets the phone, the last address said is where.
 */
static void
memprobe(void)
{
	uint64_t base, end, hz = READ_SYSREG(cntfrq_el0);

	say("memory probe: each free 16 MB is touched, then 0.3 s of waiting\n");
	for (int i = 0; efi_free_range(i, &base, &end) == 0; i++) {
		for (uint64_t chunk = base; chunk < end; chunk += 0x1000000) {
			uint64_t t;

			say(" %llx", (unsigned long long)chunk);
			for (uint64_t a = chunk; a < chunk + 0x1000000 && a < end; a += 0x10000) {
				volatile uint64_t *p = (volatile uint64_t *)(uintptr_t)a;
				*p = *p;
			}
			__asm__ volatile("dsb sy");
			t = READ_SYSREG(cntpct_el0);
			while (READ_SYSREG(cntpct_el0) - t < hz * 3 / 10) {
			}
		}
	}
	say("\nmemory probe: all of it can be written\n");
}
#endif

struct container_entry {
	char name[16];
	uint64_t offset, size;
};

struct container {
	char magic[8];                  /* "MIA1BOOT" */
	uint32_t version, count;
	struct container_entry entry[4];
};

static const struct container_entry *
find(const struct container *c, const char *name)
{
	for (uint32_t i = 0; i < c->count && i < 4; i++) {
		if (strcmp(c->entry[i].name, name) == 0) {
			return &c->entry[i];
		}
	}
	return NULL;
}

void
shim_main(const void *fdt, uint64_t loaded_at)
{
	static struct ram_bank banks[16];
	struct region reserved[8];
	const struct container *c;
	const struct container_entry *loader, *payload;
	uint64_t initrd = 0, initrd_end = 0, total = 0, largest, largest_size, payload_at;
	int nbanks, nreserved = 0;

	watchdog_off();
	nbanks = fdt_memory(fdt, banks, 16);
	if (nbanks <= 0) {
		/* nothing to go by: the region aboot itself runs in, which is RAM on every MSM8953 */
		banks[0] = (struct ram_bank){ 0x80000000, 0x40000000 };
		nbanks = 1;
	}
	mmu_on(banks, nbanks);
	display_init();
	keep_start();
	say("rpiios Mi A1 shim at %llx, EL%llu, timer %llu Hz, device tree at %p\n", (unsigned long long)loaded_at,
	    (unsigned long long)(READ_SYSREG(CurrentEL) >> 2), (unsigned long long)READ_SYSREG(cntfrq_el0), fdt);
	for (int i = 0; i < nbanks; i++) {
		say("  RAM %llx..%llx (%llu MB)\n", (unsigned long long)banks[i].base,
		    (unsigned long long)(banks[i].base + banks[i].size), (unsigned long long)(banks[i].size >> 20));
		total += banks[i].size;
	}
	say("  %llu MB in all\n", (unsigned long long)(total >> 20));
	watchdog_watch();
	if (fdt_initrd(fdt, &initrd, &initrd_end) != 0) {
		stop("aboot gave no ramdisk (no linux,initrd-start in /chosen)\n");
	}
	c = (const struct container *)(uintptr_t)initrd;
	if (memcmp(c->magic, "MIA1BOOT", 8) != 0) {
		stop("the ramdisk at %llx is not a MIA1BOOT container\n", (unsigned long long)initrd);
	}
	loader = find(c, "loader");
	payload = find(c, "payload");
	if (loader == NULL || payload == NULL || payload->size > PAYLOAD_SPACE - 0x100000) {
		stop("the container lacks the loader or the payload, or the payload is too big\n");
	}
	say("ramdisk %llx..%llx: loader %llu KB, payload %llu KB\n", (unsigned long long)initrd,
	    (unsigned long long)initrd_end, (unsigned long long)(loader->size >> 10), (unsigned long long)(payload->size >> 10));

	for (unsigned i = 0; i < sizeof(firmware) / sizeof(firmware[0]); i++) {
		reserved[nreserved++] = firmware[i];
	}
	reserved[nreserved++] = (struct region){ FB_BASE & ~0xfffull, FB_REGION_SIZE + 0x1000, 0, "display" };
	reserved[nreserved++] = (struct region){ loaded_at, (uint64_t)(_image_end - _head), 1 /* loader code */, "shim" };
	reserved[nreserved++] = (struct region){ initrd, initrd_end - initrd, 2 /* loader data */, "ramdisk" };
	efi_memory_init(banks, nbanks, reserved, nreserved);

	/*
	 * The payload stays where aboot put it, in the ramdisk (aboot's own choice of memory: known to
	 * be writable); the loader's area for it (payload, then its panic log) runs on past the ramdisk.
	 */
	payload_at = (uint64_t)(uintptr_t)c + payload->offset;
	efi_reserve(payload_at, PAYLOAD_SPACE, 2 /* loader data */);
#ifdef HIGH_ONLY
	/*
	 * mia1-ios-high.img: nothing of the low bank but what aboot itself wrote (this shim, the device
	 * tree, the ramdisk): the phone resets a moment after large writes there. The loader, the
	 * kernel and iOS in the upper bank, where aboot keeps its own download buffer.
	 */
	efi_reserve(0x10000000, 0x70000000, 0);
#endif
	largest = efi_largest_free(&largest_size);
	say("payload at %llx (in the ramdisk); the kernel's block %llx..%llx (%llu MB)\n", (unsigned long long)payload_at,
	    (unsigned long long)largest, (unsigned long long)(largest + largest_size), (unsigned long long)(largest_size >> 20));
	if (largest_size < 0x40000000) {
		stop("the largest free block is %llu MB: too little for iOS\n", (unsigned long long)(largest_size >> 20));
	}
#ifdef MEMPROBE
	memprobe();
#endif

	efi_run_image((const uint8_t *)c + loader->offset, loader->size, 0, payload_at);
	stop("the loader returned\n");
}
