/* WP-34 test library: the object the hello specimen imports from. Built by the
 * cross toolchain into an ET_DYN with no libc. It exports one function and one
 * datum, which is what makes the importer carry a JUMP_SLOT for the call and a
 * COPY or GLOB_DAT for the datum. There are no system calls. */
#include "handshake.h"

/* An exported datum. The importer reads it across the object boundary. */
uint64_t greet_word = HS_WORD;

/* An exported function. Reached through the importer's PLT. */
uint64_t greet(struct hs *h)
{
	h->greet_ret = h->in_cookie ^ HS_KEY;
	return h->greet_ret;
}
