/*
 * Xiaomi Mi A1 (tissot) boot shim: the little of a C library it uses.
 */
#include "shim.h"

void *
memset(void *d, int c, size_t n)
{
	uint8_t *p = d;

	while (n--) {
		*p++ = (uint8_t)c;
	}
	return d;
}

void *
memcpy(void *d, const void *s, size_t n)
{
	uint8_t *p = d;
	const uint8_t *q = s;

	if ((((uintptr_t)p | (uintptr_t)q | n) & 7) == 0) {
		uint64_t *p8 = d;
		const uint64_t *q8 = s;

		for (n /= 8; n--;) {
			*p8++ = *q8++;
		}
		return d;
	}
	while (n--) {
		*p++ = *q++;
	}
	return d;
}

void *
memmove(void *d, const void *s, size_t n)
{
	uint8_t *p = d;
	const uint8_t *q = s;

	if (p < q || p >= q + n) {
		return memcpy(d, s, n);
	}
	while (n--) {
		p[n] = q[n];
	}
	return d;
}

int
memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *p = a, *q = b;

	for (; n--; p++, q++) {
		if (*p != *q) {
			return *p - *q;
		}
	}
	return 0;
}

size_t
strlen(const char *s)
{
	size_t n = 0;

	while (s[n]) {
		n++;
	}
	return n;
}

int
strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++, b++;
	}
	return (uint8_t)*a - (uint8_t)*b;
}

/* %s %c %d %u %x %llx %lx %llu %lu %p %%, with a width and 0 padding */
int
vsnprintf(char *out, size_t size, const char *fmt, va_list ap)
{
	size_t n = 0;

#define PUT(ch) do { if (n + 1 < size) out[n] = (ch); n++; } while (0)
	for (; *fmt; fmt++) {
		char digits[24];
		int width = 0, zero = 0, longs = 0, len = 0;
		unsigned base = 10;
		uint64_t v;
		const char *s;

		if (*fmt != '%') {
			PUT(*fmt);
			continue;
		}
		fmt++;
		if (*fmt == '0') {
			zero = 1;
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (*fmt++ - '0');
		}
		while (*fmt == 'l') {
			longs++;
			fmt++;
		}
		switch (*fmt) {
		case 's':
			s = va_arg(ap, const char *);
			s = s ? s : "(null)";
			for (len = (int)strlen(s); width > len; width--) {
				PUT(' ');
			}
			while (*s) {
				PUT(*s++);
			}
			continue;
		case 'c':
			PUT((char)va_arg(ap, int));
			continue;
		case '%':
			PUT('%');
			continue;
		case 'p':
			v = (uint64_t)va_arg(ap, void *);
			base = 16;
			break;
		case 'x':
			base = 16;
			v = longs ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
			break;
		case 'd':
			if (longs) {
				int64_t sv = va_arg(ap, int64_t);
				if (sv < 0) {
					PUT('-');
					sv = -sv;
				}
				v = (uint64_t)sv;
			} else {
				int sv = va_arg(ap, int);
				if (sv < 0) {
					PUT('-');
					sv = -sv;
				}
				v = (unsigned)sv;
			}
			break;
		default:
			v = longs ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
			break;
		}
		do {
			unsigned d = (unsigned)(v % base);
			digits[len++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
			v /= base;
		} while (v != 0);
		for (; width > len; width--) {
			PUT(zero ? '0' : ' ');
		}
		while (len) {
			PUT(digits[--len]);
		}
	}
	if (size) {
		out[n < size ? n : size - 1] = 0;
	}
#undef PUT
	return (int)n;
}

int
snprintf(char *out, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(out, size, fmt, ap);
	va_end(ap);
	return n;
}

void
udelay(uint64_t us)
{
	uint64_t start = READ_SYSREG(cntpct_el0), ticks = us * (READ_SYSREG(cntfrq_el0) / 1000000);

	while (READ_SYSREG(cntpct_el0) - start < ticks) {
	}
}
