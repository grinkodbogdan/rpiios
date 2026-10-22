/*
 * Xiaomi Mi A1 (tissot) boot shim: just enough UEFI for the XNU loader.
 *
 * HackiOS's loader (xnuloader/loader.c) is a UEFI application. On the POCO it
 * runs under a full UEFI (Mu-Silicium with Qualcomm's own drivers); the Mi A1
 * has none -- its firmware ends in LK -- so this file is the UEFI it runs under.
 * The loader uses little of it, and that is what is here:
 *
 *   memory      a memory map (the banks aboot reports, less what the firmware,
 *               the display and the boot itself use), and pages and pool from it
 *   display     a Graphics Output protocol for the splash memory, 1080x1920
 *               XRGB8888 once console.c has switched the display to that
 *   text        ConOut: on the screen (console.c)
 *   time        Stall, by the generic timer
 *   reset       ResetSystem: does not reset -- the screen is the only log this
 *               phone has, so what is on it stays for a photograph
 *   the payload a configuration table, "MIA1 payload", with its address: the
 *               loader takes the payload from there instead of its fixed address
 *               (hackios-xnu-kernel.diff)
 *
 * Everything else answers "not found" (the storage, the USB device, the
 * charger: the loader goes on without each) or does nothing (events, the
 * watchdog). Boot services are never exited, as on the POCO.
 *
 * The loader is a PE32+ image; it is put where the shim says, its base
 * relocations applied, and its entry point called with the system table.
 */
#include "shim.h"
#include "efi.h"                /* the loader's own (HackiOS xnuloader/efi.h): the same layouts */

#define EFI_SUCCESS             0
#define EFI_INVALID_PARAMETER   (EFI_ERROR_BIT | 2)
#define EFI_UNSUPPORTED         (EFI_ERROR_BIT | 3)
#define EFI_OUT_OF_RESOURCES    (EFI_ERROR_BIT | 9)
#define EFI_NOT_FOUND           (EFI_ERROR_BIT | 14)
#define PAGE                    4096ull

/* "MIA1 payload": where the payload is (the loader looks for this table) */
#define MIA1_PAYLOAD_GUID       { 0x4d494131, 0x7061, 0x796c, { 0x6f, 0x61, 0x64, 0x00, 0x74, 0x69, 0x73, 0x73 } }

/* ---- memory ------------------------------------------------------------------------------------ */

#define MAX_RANGES 128

static struct range {
	uint64_t base, end;
	uint32_t type;
} ranges[MAX_RANGES];
static int nranges;
static uint64_t map_key = 1;

/* `type` over [base, end), wherever it falls in the map (outside RAM: nothing) */
static void
set_type(uint64_t base, uint64_t end, uint32_t type)
{
	for (int i = 0; i < nranges; i++) {
		struct range r = ranges[i];
		uint64_t lo = base > r.base ? base : r.base, hi = end < r.end ? end : r.end;

		if (lo >= hi || r.type == type) {
			continue;
		}
		if (nranges + 2 > MAX_RANGES) {
			stop("memory map full\n");
		}
		/* r -> [r.base, lo) r.type, [lo, hi) type, [hi, r.end) r.type */
		memmove(&ranges[i + 3], &ranges[i + 1], (size_t)(nranges - i - 1) * sizeof(ranges[0]));
		ranges[i] = (struct range){ r.base, lo, r.type };
		ranges[i + 1] = (struct range){ lo, hi, type };
		ranges[i + 2] = (struct range){ hi, r.end, r.type };
		nranges += 2;
		i += 2;
	}
	/* drop the empty ones, merge neighbours of one type */
	int n = 0;
	for (int i = 0; i < nranges; i++) {
		if (ranges[i].base >= ranges[i].end) {
			continue;
		}
		if (n > 0 && ranges[n - 1].end == ranges[i].base && ranges[n - 1].type == ranges[i].type) {
			ranges[n - 1].end = ranges[i].end;
			continue;
		}
		ranges[n++] = ranges[i];
	}
	nranges = n;
	map_key++;
}

void
efi_memory_init(const struct ram_bank *banks, int nbanks, const struct region *reserved, int nreserved)
{
	nranges = 0;
	for (int i = 0; i < nbanks; i++) {
		uint64_t b = (banks[i].base + PAGE - 1) & ~(PAGE - 1), e = (banks[i].base + banks[i].size) & ~(PAGE - 1);
		int at = nranges;

		while (at > 0 && ranges[at - 1].base > b) {
			ranges[at] = ranges[at - 1];
			at--;
		}
		ranges[at] = (struct range){ b, e, EfiConventionalMemory };
		nranges++;
	}
	for (int i = 0; i < nreserved; i++) {
		set_type(reserved[i].base & ~(PAGE - 1), (reserved[i].base + reserved[i].size + PAGE - 1) & ~(PAGE - 1),
		    reserved[i].type);
	}
}

void
efi_reserve(uint64_t base, uint64_t size, uint32_t type)
{
	set_type(base & ~(PAGE - 1), (base + size + PAGE - 1) & ~(PAGE - 1), type);
}

uint64_t
efi_largest_free(uint64_t *size)
{
	uint64_t best = 0, best_size = 0;

	for (int i = 0; i < nranges; i++) {
		if (ranges[i].type == EfiConventionalMemory && ranges[i].end - ranges[i].base > best_size) {
			best = ranges[i].base;
			best_size = ranges[i].end - ranges[i].base;
		}
	}
	*size = best_size;
	return best;
}

/* The i-th free range of the map, for the memory probe; -1 past the last. */
int
efi_free_range(int i, uint64_t *base, uint64_t *end)
{
	for (int k = 0; k < nranges; k++) {
		if (ranges[k].type == EfiConventionalMemory && i-- == 0) {
			*base = ranges[k].base;
			*end = ranges[k].end;
			return 0;
		}
	}
	return -1;
}

static int
all_free(uint64_t base, uint64_t end)
{
	for (int i = 0; i < nranges; i++) {
		if (ranges[i].type == EfiConventionalMemory && ranges[i].base <= base && end <= ranges[i].end) {
			return 1;
		}
	}
	return 0;
}

static efi_status EFIAPI
allocate_pages(int type, int mem_type, uint64_t pages, uint64_t *addr)
{
	uint64_t bytes = pages * PAGE, largest, largest_size;

	if (addr == NULL || pages == 0) {
		return EFI_INVALID_PARAMETER;
	}
	if (type == AllocateAddress) {
		if (!all_free(*addr, *addr + bytes)) {
			return EFI_NOT_FOUND;
		}
		set_type(*addr, *addr + bytes, (uint32_t)mem_type);
		return EFI_SUCCESS;
	}
	/* anywhere (below a limit): the lowest place that fits, outside the largest block, which the kernel is to have */
	largest = efi_largest_free(&largest_size);
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < nranges; i++) {
			struct range *r = &ranges[i];

			if (r->type != EfiConventionalMemory || r->end - r->base < bytes || (pass == 0 && r->base == largest)) {
				continue;
			}
			if (type == AllocateMaxAddress && r->base + bytes - 1 > *addr) {
				continue;
			}
			*addr = r->base;
			set_type(*addr, *addr + bytes, (uint32_t)mem_type);
			return EFI_SUCCESS;
		}
	}
	return EFI_OUT_OF_RESOURCES;
}

static efi_status EFIAPI
free_pages(uint64_t addr, uint64_t pages)
{
	set_type(addr, addr + pages * PAGE, EfiConventionalMemory);
	return EFI_SUCCESS;
}

static efi_status EFIAPI
get_memory_map(uint64_t *size, void *map, uint64_t *key, uint64_t *desc_size, uint32_t *desc_ver)
{
	uint64_t need = (uint64_t)nranges * sizeof(efi_memory_descriptor);

	if (desc_size) {
		*desc_size = sizeof(efi_memory_descriptor);
	}
	if (desc_ver) {
		*desc_ver = 1;
	}
	if (*size < need || map == NULL) {
		*size = need;
		return EFI_BUFFER_TOO_SMALL;
	}
	for (int i = 0; i < nranges; i++) {
		efi_memory_descriptor *d = (efi_memory_descriptor *)map + i;

		d->type = ranges[i].type;
		d->pad = 0;
		d->phys_start = ranges[i].base;
		d->virt_start = 0;
		d->pages = (ranges[i].end - ranges[i].base) / PAGE;
		d->attribute = 0xf;     /* UC WC WT WB */
	}
	*size = need;
	if (key) {
		*key = map_key;
	}
	return EFI_SUCCESS;
}

/* pool: pages of its own, handed out in order; never given back */
static uint64_t pool_next, pool_end;

static efi_status EFIAPI
allocate_pool(int type, uint64_t size, void **buf)
{
	size = (size + 15) & ~15ull;
	if (pool_next + size > pool_end) {
		uint64_t pages = (size + (1 << 20) + PAGE - 1) / PAGE, at = 0;

		if (allocate_pages(AllocateAnyPages, type, pages, &at) != EFI_SUCCESS) {
			return EFI_OUT_OF_RESOURCES;
		}
		pool_next = at;
		pool_end = at + pages * PAGE;
	}
	*buf = (void *)(uintptr_t)pool_next;
	pool_next += size;
	return EFI_SUCCESS;
}

static efi_status EFIAPI
free_pool(void *buf)
{
	(void)buf;
	return EFI_SUCCESS;
}

/* ---- protocols --------------------------------------------------------------------------------- */

static efi_gop_mode_info gop_info;
static efi_gop_mode gop_mode;
static efi_gop gop;

static int
same_guid(const efi_guid *a, const efi_guid *b)
{
	return memcmp(a, b, sizeof(*a)) == 0;
}

static efi_status EFIAPI
gop_query_mode(efi_gop *self, uint32_t mode, uint64_t *info_size, efi_gop_mode_info **info)
{
	(void)self;
	if (mode != 0) {
		return EFI_INVALID_PARAMETER;
	}
	*info_size = sizeof(gop_info);
	*info = &gop_info;
	return EFI_SUCCESS;
}

static efi_status EFIAPI
gop_set_mode(efi_gop *self, uint32_t mode)
{
	(void)self;
	return mode == 0 ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

static efi_status EFIAPI
locate_protocol(const efi_guid *guid, void *registration, void **iface)
{
	static const efi_guid gop_guid = EFI_GOP_GUID;

	(void)registration;
	if (same_guid(guid, &gop_guid) && display_bpp == 32) {
		*iface = &gop;
		return EFI_SUCCESS;
	}
	return EFI_NOT_FOUND;
}

static efi_status EFIAPI
handle_protocol(efi_handle h, const efi_guid *guid, void **iface)
{
	(void)h, (void)guid, (void)iface;
	return EFI_UNSUPPORTED;
}

static efi_status EFIAPI
locate_handle_buffer(int type, const efi_guid *guid, void *key, uint64_t *count, efi_handle **buf)
{
	(void)type, (void)guid, (void)key;
	*count = 0;
	*buf = NULL;
	return EFI_NOT_FOUND;
}

static efi_status EFIAPI
stall(uint64_t us)
{
	udelay(us);
	return EFI_SUCCESS;
}

static efi_status EFIAPI
set_watchdog_timer(uint64_t timeout, uint64_t code, uint64_t size, char16 *data)
{
	(void)timeout, (void)code, (void)size, (void)data;
	return EFI_SUCCESS;
}

static efi_status EFIAPI
no_event(void *event)
{
	(void)event;
	return EFI_SUCCESS;
}

static efi_status EFIAPI
create_event(uint32_t type, uint64_t tpl, void *notify, void *ctx, void **event)
{
	(void)type, (void)tpl, (void)notify, (void)ctx;
	*event = (void *)1;
	return EFI_SUCCESS;
}

static efi_status EFIAPI
create_event_ex(uint32_t type, uint64_t tpl, void (EFIAPI *notify)(void *event, void *ctx), const void *ctx,
    const efi_guid *group, void **event)
{
	(void)type, (void)tpl, (void)notify, (void)ctx, (void)group;
	*event = (void *)1;
	return EFI_SUCCESS;
}

static efi_status EFIAPI
check_event(void *event)
{
	(void)event;
	return EFI_ERROR_BIT | 6;       /* not ready */
}

static efi_status EFIAPI
exit_boot_services(efi_handle image, uint64_t key)
{
	(void)image, (void)key;
	return EFI_SUCCESS;
}

static efi_status EFIAPI
output_string(efi_text_output *self, const char16 *s)
{
	(void)self;
	for (; *s; s++) {
		console_putc(*s < 128 ? (char)*s : '?');
	}
	return EFI_SUCCESS;
}

static efi_status EFIAPI
get_time(struct efi_time *t, void *caps)
{
	(void)t, (void)caps;
	return EFI_UNSUPPORTED;
}

static void EFIAPI
reset_system(int type, efi_status status, uint64_t size, void *data)
{
	(void)size, (void)data;
	stop("\nThe loader asked for a reset (type %d, status %llx): not done, so that this screen stays.\n", type,
	    (unsigned long long)status);
}

/* ---- the tables -------------------------------------------------------------------------------- */

static efi_boot_services bs;
static efi_runtime_services rt;
static efi_system_table st;
static efi_text_output con_out;
static struct { efi_guid guid; void *table; } config[1];
static char16 vendor[] = { 'r', 'p', 'i', 'i', 'o', 's', ' ', 'M', 'i', ' ', 'A', '1', 0 };

static void
tables(uint64_t payload)
{
	static const efi_guid payload_guid = MIA1_PAYLOAD_GUID;

	bs.hdr.signature = 0x56524553544f4f42ull;     /* "BOOTSERV" */
	bs.hdr.revision = 2u << 16 | 70;
	bs.hdr.header_size = sizeof(bs);
	bs.allocate_pages = allocate_pages;
	bs.free_pages = free_pages;
	bs.get_memory_map = get_memory_map;
	bs.allocate_pool = allocate_pool;
	bs.free_pool = free_pool;
	bs.create_event = (void *)create_event;
	bs.signal_event = no_event;
	bs.close_event = no_event;
	bs.check_event = (void *)check_event;
	bs.handle_protocol = handle_protocol;
	bs.exit_boot_services = exit_boot_services;
	bs.stall = stall;
	bs.set_watchdog_timer = set_watchdog_timer;
	bs.locate_handle_buffer = locate_handle_buffer;
	bs.locate_protocol = locate_protocol;
	bs.create_event_ex = create_event_ex;

	rt.hdr.signature = 0x56524553544e5552ull;     /* "RUNTSERV" */
	rt.hdr.revision = 2u << 16 | 70;
	rt.hdr.header_size = sizeof(rt);
	rt.get_time = get_time;
	rt.reset_system = reset_system;

	con_out.output_string = output_string;

	gop_info.version = 0;
	gop_info.width = FB_WIDTH;
	gop_info.height = FB_HEIGHT;
	gop_info.pixel_format = 1;                      /* blue, green, red, reserved */
	gop_info.pixels_per_scanline = FB_WIDTH;
	gop_mode.max_mode = 1;
	gop_mode.mode = 0;
	gop_mode.info = &gop_info;
	gop_mode.info_size = sizeof(gop_info);
	gop_mode.fb_base = FB_BASE;
	gop_mode.fb_size = (uint64_t)FB_WIDTH * FB_HEIGHT * 4;
	gop.query_mode = gop_query_mode;
	gop.set_mode = gop_set_mode;
	gop.mode = &gop_mode;

	config[0].guid = payload_guid;
	config[0].table = (void *)(uintptr_t)payload;

	st.hdr.signature = 0x5453595320494249ull;     /* "IBI SYST" */
	st.hdr.revision = 2u << 16 | 70;
	st.hdr.header_size = sizeof(st);
	st.vendor = vendor;
	st.revision = 1;
	st.con_out = &con_out;
	st.std_err = &con_out;
	st.rt = &rt;
	st.bs = &bs;
	st.nr_tables = 1;
	st.tables = (void *)config;
}

/* ---- the PE image ------------------------------------------------------------------------------ */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

uint64_t
efi_run_image(const void *image, uint64_t image_size, uint64_t load_at, uint64_t payload)
{
	const uint8_t *pe = image, *coff, *opt, *sections;
	uint64_t image_base, size_of_image, delta, entry;
	uint32_t nsections, opt_size, reloc_rva, reloc_size;
	efi_status (EFIAPI *efi_main)(efi_handle, efi_system_table *);
	uint8_t *at;

	if (image_size < 0x200 || pe[0] != 'M' || pe[1] != 'Z') {
		stop("the loader is not a PE image\n");
	}
	coff = pe + rd32(pe + 0x3c);
	if (memcmp(coff, "PE\0\0", 4) != 0 || rd16(coff + 4) != 0xaa64) {
		stop("the loader is not an arm64 PE image\n");
	}
	nsections = rd16(coff + 6);
	opt_size = rd16(coff + 20);
	opt = coff + 24;
	if (rd16(opt) != 0x20b) {
		stop("the loader is not PE32+\n");
	}
	entry = rd32(opt + 16);
	image_base = rd64(opt + 24);
	size_of_image = rd32(opt + 56);
	reloc_rva = rd32(opt + 112 + 5 * 8);
	reloc_size = rd32(opt + 112 + 5 * 8 + 4);
	sections = opt + opt_size;

	if (allocate_pages(load_at ? AllocateAddress : AllocateAnyPages, 1 /* loader code */, (size_of_image + PAGE - 1) / PAGE,
	    &load_at) != EFI_SUCCESS) {
		stop("no room for the loader at %llx (%llx bytes)\n", (unsigned long long)load_at,
		    (unsigned long long)size_of_image);
	}
	at = (uint8_t *)(uintptr_t)load_at;
	memset(at, 0, size_of_image);
	memcpy(at, pe, rd32(opt + 60));                 /* the headers */
	for (uint32_t i = 0; i < nsections; i++) {
		const uint8_t *s = sections + 40 * i;
		uint32_t vsize = rd32(s + 8), va = rd32(s + 12), raw = rd32(s + 16), raw_at = rd32(s + 20);

		memcpy(at + va, pe + raw_at, raw < vsize || vsize == 0 ? raw : vsize);
	}
	delta = load_at - image_base;
	for (uint32_t off = 0; off < reloc_size;) {
		const uint8_t *block = at + reloc_rva + off;
		uint32_t page = rd32(block), bytes = rd32(block + 4);

		if (bytes < 8) {
			break;
		}
		for (uint32_t k = 8; k < bytes; k += 2) {
			uint16_t e = rd16(block + k);

			if (e >> 12 == 10) {
				uint64_t *p = (uint64_t *)(at + page + (e & 0xfff));
				*p += delta;
			} else if (e >> 12 != 0) {
				stop("the loader has a relocation of type %u\n", e >> 12);
			}
		}
		off += bytes;
	}
	icache_sync_range(load_at, size_of_image);
	tables(payload);
	say("loader: %llu KB at %llx (linked at %llx), entry +%llx\n", (unsigned long long)(size_of_image >> 10),
	    (unsigned long long)load_at, (unsigned long long)image_base, (unsigned long long)entry);
	efi_main = (void *)(at + entry);
	return efi_main((efi_handle)(uintptr_t)load_at, &st);
}
