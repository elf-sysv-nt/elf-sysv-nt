/* WP-34 test specimen: a dynamically linked "hello" with no libc.
 *
 * Built by the cross toolchain into a PIE (ET_DYN) that imports from libgreet,
 * so placing and relocating it exercises the whole engine at once:
 *
 *   - a cross-object call to greet() through the PLT   -> R_X86_64_JUMP_SLOT,
 *     lazy or eager depending on how this variant was linked;
 *   - a read of the imported datum greet_word          -> R_X86_64_COPY or
 *     R_X86_64_GLOB_DAT, whichever the linker chose;
 *   - a dereference of a pointer into its own data      -> R_X86_64_RELATIVE,
 *     or an RELR entry when linked with pack-relative-relocs;
 *   - a call to an ifunc-dispatched memcpy              -> R_X86_64_IRELATIVE,
 *     whose resolver picks a body the way glibc's memcpy does.
 *
 * It reports each result through the handshake block and returns. It is entered
 * at e_entry (the `entry` symbol) with the handshake pointer in the first
 * System V argument register.
 */
#include "handshake.h"

extern uint64_t greet(struct hs *h);   /* defined in libgreet */
extern uint64_t greet_word;            /* defined in libgreet */

/* A pointer into our own data: a RELATIVE relocation (or an RELR entry). */
static uint64_t targets[3] = { 0x1111ULL, 0x2222ULL, 0x3333ULL };
static uint64_t *const self_ptr = &targets[1];

/* ---- an ifunc-dispatched memcpy, in glibc's shape ---------------------- */

typedef void *(*memcpy_fn)(void *, const void *, unsigned long);

/* Two correct implementations that differ only in identity. The resolver's
 * choice is recorded so the harness can read which body a real loader's
 * criterion selected on this CPU. */
unsigned long chosen_impl;   /* 1 = baseline, 2 = erms; set by the resolver */

static void *memcpy_baseline(void *d, const void *s, unsigned long n)
{
	unsigned char *dd = d;
	const unsigned char *ss = s;
	unsigned long i;
	for (i = 0; i < n; i++) dd[i] = ss[i];
	return d;
}

static void *memcpy_erms(void *d, const void *s, unsigned long n)
{
	unsigned char *dd = d;
	const unsigned char *ss = s;
	unsigned long i;
	for (i = 0; i < n; i++) dd[i] = ss[i];
	return d;
}

/* Does the CPU advertise Enhanced REP MOVSB? CPUID leaf 7, EBX bit 9 -- one of
 * the very criteria glibc's memcpy ifunc resolver tests. */
static int have_erms(void)
{
	unsigned int a, b, c, d;
	__asm__ volatile ("cpuid"
	                  : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
	                  : "a"(7u), "c"(0u));
	return (b >> 9) & 1u;
}

static memcpy_fn resolve_memcpy(void)
{
	if (have_erms()) { chosen_impl = 2; return memcpy_erms; }
	chosen_impl = 1; return memcpy_baseline;
}

/* The ifunc symbol. Its relocation is R_X86_64_IRELATIVE; the loader runs the
 * resolver and stores the body it returns. */
void *my_memcpy(void *, const void *, unsigned long)
	__attribute__((ifunc("resolve_memcpy")));

/* ---- entry ------------------------------------------------------------- */

void entry(struct hs *h)
{
	char src[16];
	char dst[16];
	int i, ok = 1;

	h->ran = 1;

	/* cross-object call through the PLT */
	h->greet_ret = greet(h);

	/* imported datum */
	h->data_word = greet_word;

	/* internal relocated pointer */
	h->relative_ok = (*self_ptr == 0x2222ULL);

	/* ifunc memcpy */
	for (i = 0; i < 16; i++) src[i] = (char)(i + 1);
	for (i = 0; i < 16; i++) dst[i] = 0;
	my_memcpy(dst, src, 16);
	for (i = 0; i < 16; i++) if (dst[i] != src[i]) ok = 0;
	h->memcpy_ok = ok;
	h->impl_id = chosen_impl;
}
