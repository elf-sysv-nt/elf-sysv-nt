/* WP-34 test handshake: the block the harness hands a mapped specimen and
 * reads back. The specimen is entered at its e_entry with a pointer to this in
 * the first System V argument register -- the same private convention WP-32's
 * mapping test used, since the real psABI stack is WP-40's. No system calls are
 * made: there is no Linux kernel underneath, only the re-faced Cygwin floor. */
#ifndef ELFSYSV_LOADER_RELOC_T_HANDSHAKE_H
#define ELFSYSV_LOADER_RELOC_T_HANDSHAKE_H

#include <stdint.h>

#define HS_KEY   0x9E3779B97F4A7C15ULL
#define HS_WORD  0xABCDEF0011223344ULL   /* the datum libgreet exports */

struct hs {
	uint64_t in_cookie;    /* input the harness sets */
	uint64_t ran;          /* set nonzero once the entry ran */
	uint64_t greet_ret;    /* return of the cross-object call through the PLT */
	uint64_t data_word;    /* value read from the imported datum */
	uint64_t relative_ok;  /* an internal RELATIVE/RELR pointer dereferenced */
	uint64_t memcpy_ok;    /* the ifunc memcpy copied correctly */
	uint64_t impl_id;      /* which ifunc body the resolver chose (its address) */
};

#endif
