/*
 * Xiaomi Mi A1 (tissot) boot shim: what aboot wrote into the device tree.
 *
 * aboot fills in the memory banks (from the RAM partition table in shared
 * memory) and, in /chosen, where it put the ramdisk of the boot image. Our tree
 * has #address-cells = #size-cells = 2 at the root (tissot.dts).
 */
#include "shim.h"

#define FDT_MAGIC       0xd00dfeed
#define FDT_BEGIN_NODE  1
#define FDT_END_NODE    2
#define FDT_PROP        3
#define FDT_NOP         4
#define FDT_END         9

static uint32_t
be32(const void *p)
{
	const uint8_t *b = p;

	return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

static uint64_t
cells(const uint8_t *p, int n)
{
	return n == 2 ? (uint64_t)be32(p) << 32 | be32(p + 4) : be32(p);
}

/* Each property of each node at depth 1 (and the root's own, as node ""), to `each`. */
static int
walk(const void *fdt, void (*each)(void *ctx, const char *node, const char *name, const uint8_t *value, uint32_t len),
    void *ctx)
{
	const uint8_t *base = fdt;
	const uint8_t *p, *strings;
	const char *node = "";
	int depth = 0;

	if (be32(base) != FDT_MAGIC) {
		return -1;
	}
	p = base + be32(base + 8);
	strings = base + be32(base + 12);
	for (;;) {
		uint32_t token = be32(p);

		p += 4;
		if (token == FDT_BEGIN_NODE) {
			const char *name = (const char *)p;

			depth++;
			if (depth == 2) {
				node = name;
			}
			p += (strlen(name) + 4) & ~3u;
		} else if (token == FDT_END_NODE) {
			depth--;
			if (depth == 1) {
				node = "";
			}
		} else if (token == FDT_PROP) {
			uint32_t len = be32(p), name = be32(p + 4);

			if (depth <= 2) {
				each(ctx, node, (const char *)strings + name, p + 8, len);
			}
			p += 8 + ((len + 3) & ~3u);
		} else if (token == FDT_NOP) {
			continue;
		} else {
			return 0;
		}
	}
}

struct mem_ctx {
	struct ram_bank *banks;
	int max, n, acells, scells;
};

static void
mem_each(void *c, const char *node, const char *name, const uint8_t *value, uint32_t len)
{
	struct mem_ctx *m = c;

	if (node[0] == 0) {
		if (strcmp(name, "#address-cells") == 0) {
			m->acells = (int)be32(value);
		} else if (strcmp(name, "#size-cells") == 0) {
			m->scells = (int)be32(value);
		}
		return;
	}
	if (memcmp(node, "memory", 6) != 0 || strcmp(name, "reg") != 0) {
		return;
	}
	for (uint32_t at = 0; at + 4u * (m->acells + m->scells) <= len && m->n < m->max; at += 4u * (m->acells + m->scells)) {
		uint64_t b = cells(value + at, m->acells), s = cells(value + at + 4 * m->acells, m->scells);

		if (s != 0) {
			m->banks[m->n].base = b;
			m->banks[m->n].size = s;
			m->n++;
		}
	}
}

int
fdt_memory(const void *fdt, struct ram_bank *banks, int max)
{
	struct mem_ctx m = { .banks = banks, .max = max, .acells = 2, .scells = 2 };

	if (walk(fdt, mem_each, &m) != 0) {
		return -1;
	}
	return m.n;
}

struct initrd_ctx {
	uint64_t start, end;
};

static void
initrd_each(void *c, const char *node, const char *name, const uint8_t *value, uint32_t len)
{
	struct initrd_ctx *r = c;

	if (strcmp(node, "chosen") != 0) {
		return;
	}
	if (strcmp(name, "linux,initrd-start") == 0) {
		r->start = cells(value, len >= 8 ? 2 : 1);
	} else if (strcmp(name, "linux,initrd-end") == 0) {
		r->end = cells(value, len >= 8 ? 2 : 1);
	}
}

struct bootarg_ctx {
	const char *key;
	uint32_t value;
};

static void
bootarg_each(void *c, const char *node, const char *name, const uint8_t *value, uint32_t len)
{
	struct bootarg_ctx *b = c;
	size_t klen = strlen(b->key);

	if (strcmp(node, "chosen") != 0 || strcmp(name, "bootargs") != 0) {
		return;
	}
	for (uint32_t i = 0; i + klen < len; i++) {
		if ((i == 0 || value[i - 1] == ' ') && memcmp(value + i, b->key, klen) == 0 && value[i + klen] == '=') {
			b->value = 0;
			for (uint32_t k = i + klen + 1; k < len && value[k] >= '0' && value[k] <= '9'; k++) {
				b->value = b->value * 10 + (value[k] - '0');
			}
		}
	}
}

/* KEY=NUMBER in /chosen/bootargs (the boot image's command line, with what aboot adds); 0 if absent */
uint32_t
fdt_bootarg_u32(const void *fdt, const char *key)
{
	struct bootarg_ctx b = { key, 0 };

	walk(fdt, bootarg_each, &b);
	return b.value;
}

int
fdt_initrd(const void *fdt, uint64_t *start, uint64_t *end)
{
	struct initrd_ctx r = { 0, 0 };

	if (walk(fdt, initrd_each, &r) != 0 || r.end <= r.start) {
		return -1;
	}
	*start = r.start;
	*end = r.end;
	return 0;
}
