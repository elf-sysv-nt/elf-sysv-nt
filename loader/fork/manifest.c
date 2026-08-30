/* WP-42: the manifest of reservations the host's fork does not replay.
 *
 * The parent writes it, the child reads it, and between those two moments it is
 * a block of bytes in a process the child does not control the history of. It
 * is therefore parsed as hostile input: nothing in the buffer is used to index
 * the buffer before it has been checked against the buffer's length, and every
 * invariant the packer maintains is re-established by the unpacker rather than
 * trusted. See README.md and DR-0029.
 */
#include <string.h>

#include "fork.h"

/* Header: magic, version, count, then `count` fixed-width region records. */
#define MF_MAGIC   UINT32_C(0x4b524f46)   /* "FORK" */
#define MF_VERSION UINT32_C(1)

#define MF_HDR  12u
#define MF_REC  (8u + 8u + 4u + 4u + ELF_FORK_WHAT_MAX)

static void put32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)(v);
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

static void put64(unsigned char *p, uint64_t v)
{
	put32(p, (uint32_t)v);
	put32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get64(const unsigned char *p)
{
	return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

static void say(char *why, size_t cap, const char *msg)
{
	if (why == NULL || cap == 0)
		return;
	size_t n = strlen(msg);
	if (n >= cap)
		n = cap - 1;
	memcpy(why, msg, n);
	why[n] = '\0';
}

size_t elf_fork_manifest_size(uint32_t n)
{
	return (size_t)MF_HDR + (size_t)n * (size_t)MF_REC;
}

int elf_fork_manifest_pack(const elf_fork_state *fs, unsigned char *buf,
                           size_t cap, size_t *used)
{
	if (fs == NULL || buf == NULL || used == NULL)
		return -1;

	size_t need = elf_fork_manifest_size(fs->region_count);
	if (cap < need)
		return -1;

	memset(buf, 0, need);
	put32(buf, MF_MAGIC);
	put32(buf + 4, MF_VERSION);
	put32(buf + 8, fs->region_count);

	unsigned char *p = buf + MF_HDR;
	for (uint32_t i = 0; i < fs->region_count; i++) {
		const elf_fork_region *r = &fs->region[i];
		put64(p, r->base);
		put64(p + 8, r->size);
		put32(p + 16, r->kind);
		put32(p + 20, r->prot);
		/* what[] is NUL-filled by the memset above and the source is always
		 * NUL-terminated within its field, so the copy cannot run past it. */
		memcpy(p + 24, r->what, ELF_FORK_WHAT_MAX - 1);
		p += MF_REC;
	}

	*used = need;
	return 0;
}

int elf_fork_manifest_unpack(const unsigned char *buf, size_t len,
                             elf_fork_region *out, char *why, size_t why_cap)
{
	if (out == NULL) {
		say(why, why_cap, "no output array");
		return -1;
	}
	if (buf == NULL || len < MF_HDR) {
		say(why, why_cap, "manifest shorter than its header");
		return -1;
	}
	if (get32(buf) != MF_MAGIC) {
		say(why, why_cap, "manifest magic is not FORK");
		return -1;
	}
	if (get32(buf + 4) != MF_VERSION) {
		say(why, why_cap, "manifest version is not 1");
		return -1;
	}

	uint32_t count = get32(buf + 8);
	if (count > ELF_FORK_REGION_MAX) {
		say(why, why_cap, "manifest claims more regions than the bound");
		return -1;
	}

	/* The count is bounded before it is multiplied, so the size cannot
	 * overflow, and the length is required to match exactly: a buffer with
	 * bytes past the last region is a buffer whose count and contents
	 * disagree, and there is no reading of that which is safe to prefer. */
	size_t need = elf_fork_manifest_size(count);
	if (len != need) {
		say(why, why_cap, "manifest length does not match its count");
		return -1;
	}

	const unsigned char *p = buf + MF_HDR;
	uint64_t prev_end = 0;
	for (uint32_t i = 0; i < count; i++) {
		elf_fork_region *r = &out[i];
		memset(r, 0, sizeof(*r));
		r->base = get64(p);
		r->size = get64(p + 8);
		r->kind = get32(p + 16);
		r->prot = get32(p + 20);

		if (r->size == 0) {
			say(why, why_cap, "a manifest region has zero size");
			return -1;
		}
		if (r->base > UINT64_MAX - r->size) {
			say(why, why_cap, "a manifest region wraps the address space");
			return -1;
		}
		if (r->kind != elf_fork_region_reserve &&
		    r->kind != elf_fork_region_commit) {
			say(why, why_cap, "a manifest region has an unknown kind");
			return -1;
		}
		/* Sorted ascending and disjoint. The packer keeps the table in that
		 * order, so a manifest that is not is one that was rewritten, and the
		 * child would otherwise reserve one range twice and refuse itself. */
		if (i > 0 && r->base < prev_end) {
			say(why, why_cap, "manifest regions overlap or are unsorted");
			return -1;
		}
		prev_end = r->base + r->size;

		const unsigned char *w = p + 24;
		size_t j = 0;
		while (j < ELF_FORK_WHAT_MAX && w[j] != 0)
			j++;
		if (j >= ELF_FORK_WHAT_MAX) {
			say(why, why_cap, "a manifest region name is not terminated");
			return -1;
		}
		memcpy(r->what, w, j);
		r->what[j] = '\0';

		p += MF_REC;
	}

	return (int)count;
}
