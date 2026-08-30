/* WP-32 test specimen: a static ELF that proves the mapping worked.
 *
 * Built by the cross toolchain into an ET_EXEC with no libc and no dynamic
 * relocations, so the mapping test exercises only WP-32 and nothing past it.
 * It is entered at _start with a pointer to a handshake block in the first
 * System V argument register -- a convention private to this test, since the
 * real psABI stack is WP-40's -- and it reports four things through that block
 * and returns:
 *
 *   out_bss     the value it read from .bss BEFORE writing, which must be zero
 *               if freshly committed pages arrived zeroed as spike 2 found;
 *   out_rodata  a word read from the read-only segment, proving that segment
 *               is mapped and carries the right bytes;
 *   out_rip     an instruction pointer sampled inside its own text;
 *   out_magic   the input magic transformed, proving it ran and could reach
 *               the handshake it was handed.
 *
 * It then writes .bss, proving that segment is writable, and returns to the
 * trampoline. There are no system calls: there is no Linux kernel underneath.
 */
#include <stdint.h>

struct hs {
	uint64_t in_magic;
	uint64_t out_magic;
	uint64_t out_rodata;
	uint64_t out_bss;
	uint64_t out_rip;
};

#define KEY 0x9E3779B97F4A7C15ULL

/* volatile so the read is a real load from the read-only segment rather than a
 * compile-time constant folded into the text. */
static const volatile uint64_t rodata_word = 0xC0FFEE0FBADC0DE5ULL;

/* uninitialized: lands in .bss, whose memsz exceeds its filesz. */
static uint64_t bss_word;

void _start(struct hs *h)
{
	uint64_t rip;
	__asm__ volatile ("leaq 0(%%rip), %0" : "=r"(rip));

	h->out_bss = bss_word;            /* read before writing: must be zero */
	bss_word = 0x0D15EA5EULL;         /* prove the segment is writable */
	h->out_rodata = rodata_word;      /* prove the read-only segment is there */
	h->out_rip = rip;                 /* prove control ran inside our text */
	h->out_magic = h->in_magic ^ KEY; /* prove we ran and reached the block */
}
