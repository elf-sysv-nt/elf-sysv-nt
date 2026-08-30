/* WP-34 RELR unit.
 *
 * Neither this platform's toolchain nor el8 emits RELR, so the engine's RELR
 * decoder is certified here directly, over a constructed stream, rather than
 * over a linked object. The stream mixes both entry forms -- an address word
 * and a 63-word bitmap -- and the check is that exactly the named words were
 * relocated by the bias and no others.
 */
#include "../elf_reloc.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
	uint64_t mem[128];
	uint64_t bias = (uint64_t)(uintptr_t) mem;   /* link base 0 maps here */
	unsigned i;
	int failures = 0;

	/* Prefill each word with its own link-relative value. */
	for (i = 0; i < 128; i++) mem[i] = i;

	/* An address entry for mem[1]; a bitmap relocating mem[2], mem[4], mem[10]
	 * (bits 0, 2, 8 of the word past mem[1]); an address entry for mem[100]. */
	uint64_t relr[3];
	relr[0] = 1u * 8u;                                 /* even: byte offset 8 */
	relr[1] = (((1ull << 0) | (1ull << 2) | (1ull << 8)) << 1) | 1u; /* bitmap */
	relr[2] = 100u * 8u;                               /* even: byte offset 800 */

	elf_reloc_relr(bias, relr, 3);

	/* The relocated set. */
	unsigned reloc[] = { 1, 2, 4, 10, 100 };
	unsigned nreloc = sizeof reloc / sizeof reloc[0];

	for (i = 0; i < 128; i++) {
		int should = 0;
		unsigned j;
		for (j = 0; j < nreloc; j++) if (reloc[j] == i) should = 1;
		uint64_t want = should ? ((uint64_t) i + bias) : (uint64_t) i;
		if (mem[i] != want) {
			printf("    mem[%u] = 0x%llx, wanted 0x%llx  FAILED\n",
			       i, (unsigned long long) mem[i], (unsigned long long) want);
			failures++;
		}
	}

	printf("    %u words, %u relocated by the bias, %u wrong\n",
	       128u, nreloc, (unsigned) failures);
	printf("\ncase_failures=%d\ncase_result=%s\n", failures,
	       failures ? "fail" : "pass");
	return failures ? 1 : 0;
}
