/*
 * vma.c -- the mapped-range tree, and the /proc/self/maps rendering over it.
 *
 * A fixed array, kept sorted, is the right shape for Phase 1: the process has
 * three or four ranges and the tree is walked far more often than it is
 * changed.  When mmap and fork arrive and a process carries hundreds, this
 * becomes a real tree behind the same four functions, which is why callers get
 * vma_count/vma_at rather than the array.
 */
#include <stdio.h>
#include <string.h>

#include "vma.h"

static struct vma g_vma[VMA_MAX];
static size_t g_n;

void vma_reset(void)
{
	g_n = 0;
	memset(g_vma, 0, sizeof g_vma);
}

size_t vma_count(void)
{
	return g_n;
}

const struct vma *vma_at(size_t i)
{
	return i < g_n ? &g_vma[i] : NULL;
}

int vma_record(uint64_t start, size_t len, int prot,
	       enum sub_backing_kind kind, uint64_t offset, const char *name)
{
	uint64_t end = start + len;
	size_t i, at;

	if (len == 0 || end < start || g_n == VMA_MAX)
		return -1;

	/* Refuse an overlap rather than absorbing it.  Two ranges claiming the
	 * same address means the core lost track of a mapping, and a map that
	 * quietly merges them hides the bug at the exact moment it matters. */
	for (i = 0; i < g_n; i++)
		if (start < g_vma[i].end && g_vma[i].start < end)
			return -1;

	for (at = 0; at < g_n && g_vma[at].start < start; at++)
		;
	memmove(&g_vma[at + 1], &g_vma[at], (g_n - at) * sizeof g_vma[0]);

	g_vma[at].start = start;
	g_vma[at].end = end;
	g_vma[at].prot = prot;
	g_vma[at].kind = kind;
	g_vma[at].offset = offset;
	g_vma[at].name[0] = '\0';
	if (name) {
		strncpy(g_vma[at].name, name, VMA_NAME_MAX - 1);
		g_vma[at].name[VMA_NAME_MAX - 1] = '\0';
	}
	g_n++;
	return 0;
}

/*
 * Linux writes a private mapping's fourth permission character as 'p' and a
 * shared one as 's'.  Nothing here is shared yet, and writing 'p' for a range
 * that will one day be MAP_SHARED would be a lie the day it is; the kind is
 * carried in the tree so the character can follow it when that day comes.
 */
static void perms(const struct vma *v, char out[5])
{
	out[0] = (v->prot & SUB_PROT_READ)  ? 'r' : '-';
	out[1] = (v->prot & SUB_PROT_WRITE) ? 'w' : '-';
	out[2] = (v->prot & SUB_PROT_EXEC)  ? 'x' : '-';
	out[3] = (v->kind == SUB_BACKING_SECTION) ? 's' : 'p';
	out[4] = '\0';
}

size_t vma_render_maps(char *buf, size_t cap)
{
	size_t used = 0, i;

	if (!buf || cap == 0)
		return 0;
	buf[0] = '\0';

	for (i = 0; i < g_n; i++) {
		const struct vma *v = &g_vma[i];
		char line[192], p[5];
		int n;

		perms(v, p);
		/* The device and inode columns are zeros: nothing here is
		 * backed by a file the kernel can name yet.  The columns stay
		 * because a reader parses by position, and el8's own tools do. */
		n = snprintf(line, sizeof line,
			     "%012llx-%012llx %s %08llx 00:00 0 %*s%s\n",
			     (unsigned long long)v->start,
			     (unsigned long long)v->end, p,
			     (unsigned long long)v->offset,
			     v->name[0] ? 20 : 0, "", v->name);
		if (n < 0)
			return used;
		if (used + (size_t)n + 1 > cap)
			break;		/* truncate whole lines, never half one */
		memcpy(buf + used, line, (size_t)n);
		used += (size_t)n;
		buf[used] = '\0';
	}
	return used;
}
