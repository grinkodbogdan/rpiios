/*
 * Xiaomi Mi A1 (tissot) boot shim: what its parts share.
 */
#ifndef SHIM_H
#define SHIM_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define READ_SYSREG(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " #r : "=r"(_v)); _v; })
#define WRITE_SYSREG(r, v) __asm__ volatile("msr " #r ", %0" : : "r"((uint64_t)(v)))

/* the display, as aboot leaves it: the panel in video mode, scanned out of its splash memory */
#define FB_BASE         0x90001000UL
#define FB_REGION_SIZE  0x013ff000UL
#define FB_WIDTH        1080u
#define FB_HEIGHT       1920u

/* libc.c */
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int vsnprintf(char *out, size_t size, const char *fmt, va_list ap);
int snprintf(char *out, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void udelay(uint64_t us);

/* console.c */
void display_init(void);
void console_putc(char c);
void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void stop(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
extern uint32_t display_bpp;          /* 32 once the display has been switched, 24 before */

/* fdt.c */
struct ram_bank {
	uint64_t base, size;
};
int fdt_memory(const void *fdt, struct ram_bank *banks, int max);
int fdt_initrd(const void *fdt, uint64_t *start, uint64_t *end);

/* mmu.c */
void mmu_on(const struct ram_bank *banks, int nbanks);
void cache_clean_range(uint64_t start, uint64_t length);
void icache_sync_range(uint64_t start, uint64_t length);

/* efi.c */
struct region {
	uint64_t base, size;
	uint32_t type;                  /* EFI memory type */
	const char *what;
};
void efi_memory_init(const struct ram_bank *banks, int nbanks, const struct region *reserved, int nreserved);
uint64_t efi_largest_free(uint64_t *size);
void efi_reserve(uint64_t base, uint64_t size, uint32_t type);
uint64_t efi_run_image(const void *pe, uint64_t pe_size, uint64_t load_at, uint64_t payload);

#endif
