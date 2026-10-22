/*
 * Xiaomi Mi A1 (tissot) boot shim: an identity map, and the MMU and caches on.
 *
 * aboot starts us with the MMU off, where all memory is "device" memory: no
 * caches, and an unaligned access faults -- compiled C does those, and so does
 * the XNU loader, which expects what UEFI gives it: every address mapped to
 * itself, RAM cacheable. So: 2 MB blocks over the first 8 GB, RAM (the banks
 * aboot reports) normal and write-back, the display's memory normal and not
 * cached (the display processor reads it from DRAM), as is the 2 MB with the persistent log
 * (console.c, 0x9ff00000), everything else device.
 */
#include "shim.h"

#define GB              (1ull << 30)
#define MB2             (2ull << 20)
#define SPAN_GB         8

#define ATTR_DEVICE     0
#define ATTR_NORMAL     1
#define ATTR_NC         2
#define MAIR_VALUE      (0x04ull | 0xffull << 8 | 0x44ull << 16)

#define BLOCK           1ull
#define TABLE           3ull
#define ATTRIDX(n)      ((uint64_t)(n) << 2)
#define SH_INNER        (3ull << 8)
#define AF              (1ull << 10)
#define PXN             (1ull << 53)
#define UXN             (1ull << 54)

static uint64_t l1[512] __attribute__((aligned(4096)));
static uint64_t l2[SPAN_GB][512] __attribute__((aligned(4096)));

static int
in_ram(const struct ram_bank *banks, int n, uint64_t a, uint64_t size)
{
	for (int i = 0; i < n; i++) {
		if (a >= banks[i].base && a + size <= banks[i].base + banks[i].size) {
			return 1;
		}
	}
	return 0;
}

void
mmu_on(const struct ram_bank *banks, int nbanks)
{
	uint64_t sctlr;

	for (int g = 0; g < SPAN_GB; g++) {
		for (int i = 0; i < 512; i++) {
			uint64_t a = (uint64_t)g * GB + (uint64_t)i * MB2, d = a | BLOCK | AF;

			if ((a + MB2 > (FB_BASE & ~(MB2 - 1)) && a < FB_BASE + FB_REGION_SIZE) || a == 0x9fe00000) {
				d |= ATTRIDX(ATTR_NC) | SH_INNER;
			} else if (in_ram(banks, nbanks, a, MB2)) {
				d |= ATTRIDX(ATTR_NORMAL) | SH_INNER;
			} else {
				d |= ATTRIDX(ATTR_DEVICE) | PXN | UXN;
			}
			l2[g][i] = d;
		}
		l1[g] = (uint64_t)(uintptr_t)l2[g] | TABLE;
	}
	__asm__ volatile("dsb sy");
	WRITE_SYSREG(mair_el1, MAIR_VALUE);
	/* T0SZ 25 (39 bits, from level 1), write-back walks, inner shareable, 4 KB granule, no TTBR1, 40-bit PA */
	WRITE_SYSREG(tcr_el1, 25ull | 1ull << 8 | 1ull << 10 | 3ull << 12 | 1ull << 23 | 2ull << 32);
	WRITE_SYSREG(ttbr0_el1, (uint64_t)(uintptr_t)l1);
	__asm__ volatile("isb; tlbi vmalle1; dsb sy; ic iallu; dsb sy; isb");
	sctlr = READ_SYSREG(sctlr_el1);
	sctlr |= 1ull << 0 | 1ull << 2 | 1ull << 12;    /* M, C, I */
	sctlr &= ~(1ull << 1);                          /* no alignment checks */
	WRITE_SYSREG(sctlr_el1, sctlr);
	__asm__ volatile("isb");
}

void
cache_clean_range(uint64_t start, uint64_t length)
{
	for (uint64_t a = start & ~63ull; a < start + length; a += 64) {
		__asm__ volatile("dc cvac, %0" : : "r"(a) : "memory");
	}
	__asm__ volatile("dsb sy");
}

void
icache_sync_range(uint64_t start, uint64_t length)
{
	for (uint64_t a = start & ~63ull; a < start + length; a += 64) {
		__asm__ volatile("dc cvau, %0" : : "r"(a) : "memory");
	}
	__asm__ volatile("dsb ish; ic iallu; dsb ish; isb");
}
