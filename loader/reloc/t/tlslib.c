/* WP-34 TLS specimen: an ET_DYN carrying one initial-exec thread-local, which
 * makes the linker emit an R_X86_64_TPOFF64 into a GOT slot. The harness maps
 * and relocates it and checks the stored offset against the static-TLS layout,
 * the value a real loader computes at relocation time. It is never entered --
 * a live thread pointer is WP-40/WP-41's, and DR-0000 is why %fs cannot carry
 * one here -- so this proves the arithmetic, not the access. */
#include <stdint.h>

__attribute__((tls_model("initial-exec")))
__thread uint64_t tls_word = 0x1234567800000000ULL;

/* Force a GOT reference with a TPOFF64 relocation. */
uint64_t *tls_word_addr(void)
{
	return &tls_word;
}
