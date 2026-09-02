/* WP-32: placing a validated ELF object in memory. See elf_map.h for the
 * contract and loader/map/README.md for the reserve/commit/protect sequence
 * and the host-visibility guarantee.
 *
 * The shape of the work is spike 2's: reserve the whole span, commit it, copy
 * each segment in, and only then apply protections, in a second pass because
 * two segments can share a page and a page made read-only before its neighbour
 * is filled makes the fill fault. What differs from the spike is the
 * primitive. The spike reserved and committed through VirtualAlloc directly,
 * which the runtime's memory bookkeeping never sees; this package goes through
 * mmap and mprotect so that it does, because a fork has to replay every
 * mapping and it can only replay what it recorded. The price of that choice,
 * and why the whole span is committed at once rather than a segment at a time,
 * is in doc/decisions/0008-mmap-granule-protection.md.
 */
#include "elf_map.h"
#include "host_mem.h"
#include "../elf/elf_types.h"

#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#ifdef ELFSYSV_REALPROC
/* Under the faced-runtime crossing host (DR-0071) this unit's process runtime
 * is the System V elfsysv1.dll, not host cygwin, so its mmap/mprotect/munmap
 * must cross the DR-0066 Microsoft<->System V boundary through sysv_abi thunks
 * resolved from the faced DLL -- the seam realproc-cross.c crosses for output.
 * A direct call would cross the boundary the wrong way and fault. The plain-PE
 * stub compiles without ELFSYSV_REALPROC and keeps the native, non-crossing
 * calls, so its map path is byte-for-byte unchanged. The function-like macros
 * intercept the call sites without editing them; they expand only where the
 * name is invoked as a call, after <sys/mman.h>'s declarations are parsed. */
void *rp_mmap(void *addr, size_t len, int prot, int flags, int fd,
	      long long off);
int   rp_mprotect(void *addr, size_t len, int prot);
int   rp_munmap(void *addr, size_t len);
#define mmap(a, l, p, f, fd, o)  rp_mmap((a), (l), (p), (f), (fd), (o))
#define mprotect(a, l, p)        rp_mprotect((a), (l), (p))
#define munmap(a, l)             rp_munmap((a), (l))
#endif

/* The parser keeps p_flags verbatim in elf_load_seg.flags. */
#ifndef PF_X
#define PF_X 1u
#define PF_W 2u
#define PF_R 4u
#endif

#define align_down(x, a) ((x) & ~((a) - UINT64_C(1)))
#define align_up(x, a)   (((x) + (a) - UINT64_C(1)) & ~((a) - UINT64_C(1)))

static elf_map_err fail(elf_map_diag *d, elf_map_err code, const char *field,
                        const char *fmt, ...)
{
	va_list ap;
	d->code = code;
	d->field = field;
	va_start(ap, fmt);
	vsnprintf(d->msg, sizeof d->msg, fmt, ap);
	va_end(ap);
	return code;
}

const char *elf_map_err_name(elf_map_err code)
{
	switch (code) {
	case elf_map_ok:           return "elf_map_ok";
	case elf_map_err_arg:      return "elf_map_err_arg";
	case elf_map_err_span:     return "elf_map_err_span";
	case elf_map_err_granule:  return "elf_map_err_granule";
	case elf_map_err_reserve:  return "elf_map_err_reserve";
	case elf_map_err_commit:   return "elf_map_err_commit";
	case elf_map_err_protect:  return "elf_map_err_protect";
	case elf_map_err_bss:      return "elf_map_err_bss";
	}
	return "elf_map_err_unknown";
}

/* PF_* to an mmap/mprotect protection. Absent PF_R still yields no read, which
 * matches the format; a segment with no flags becomes PROT_NONE. */
static int prot_of(uint32_t flags)
{
	int p = 0;
	if (flags & PF_R) p |= PROT_READ;
	if (flags & PF_W) p |= PROT_WRITE;
	if (flags & PF_X) p |= PROT_EXEC;
	return p;
}

/* Record PT_GNU_STACK by re-reading the program-header table. The parser
 * proved the table lies in the image and does not surface this segment, so the
 * mapper reads it back through a bounds-checked, unaligned copy of its own. */
static void record_gnu_stack(const unsigned char *image, size_t image_size,
                             const elf_parsed *p, elf_mapping *out)
{
	uint64_t i;
	for (i = 0; i < p->phnum; i++) {
		Elf64_Phdr ph;
		uint64_t at = p->phoff + i * p->phentsize;
		if (at + sizeof ph > image_size)
			return;
		memcpy(&ph, image + at, sizeof ph);
		if (ph.p_type == PT_GNU_STACK) {
			out->has_gnu_stack = 1;
			out->stack_exec = (ph.p_flags & PF_X) != 0;
			return;
		}
	}
}

/* Translate the relro file range to a runtime page range through the load
 * segment that backs it. Leaves has_relro clear if no load segment does. */
static void record_relro(const elf_parsed *p, uint64_t bias, uint64_t page,
                         elf_mapping *out)
{
	unsigned i;
	if (!p->has_relro)
		return;
	for (i = 0; i < p->load_count; i++) {
		const elf_load_seg *s = &p->load[i];
		if (p->relro_off >= s->off && p->relro_off < s->off + s->filesz) {
			uint64_t v = s->vaddr + (p->relro_off - s->off) + bias;
			out->has_relro = 1;
			out->relro_lo = align_down(v, page);
			out->relro_hi = align_up(v + p->relro_size, page);
			return;
		}
	}
}

elf_map_err elf_map(const unsigned char *image, size_t image_size,
                    const elf_parsed *p, uint64_t base_hint,
                    elf_mapping *out, elf_map_diag *diag)
{
	uint64_t page, granule, bias;
	uint64_t lo = UINT64_MAX, hi = 0, res_base, res_size;
	unsigned i, j;
	void *got;

	if (!image || !p || !out || !diag)
		return elf_map_err_arg;
	memset(out, 0, sizeof *out);
	memset(diag, 0, sizeof *diag);
	if (p->load_count == 0)
		return fail(diag, elf_map_err_arg, "load_count",
		            "the parsed object carries no PT_LOAD");

	page = elf_map_host_page_size();
	granule = elf_map_host_granule();
	out->page_size = page;
	out->granule = granule;

	/* Load bias. An ET_EXEC is honored at its link addresses; an ET_DYN is
	 * placed at base_hint, rounded up to a granule so the reservation lands
	 * where the host will accept it. */
	if (p->e_type == ET_DYN) {
		uint64_t min_v = UINT64_MAX;
		for (i = 0; i < p->load_count; i++)
			if (p->load[i].vaddr < min_v)
				min_v = p->load[i].vaddr;
		bias = align_up(base_hint, granule) - align_down(min_v, page);
	} else {
		bias = 0;
	}
	out->load_bias = bias;
	out->entry = p->e_entry + bias;

	/* The runtime span across every PT_LOAD. */
	for (i = 0; i < p->load_count; i++) {
		uint64_t v = p->load[i].vaddr + bias;
		uint64_t end = v + p->load[i].memsz;
		if (end < v)
			return fail(diag, elf_map_err_span, "p_vaddr",
			            "segment %u wraps past 2^64", i);
		if (v < lo) lo = v;
		if (end > hi) hi = end;
	}
	if (hi <= lo)
		return fail(diag, elf_map_err_span, "span",
		            "the object has an empty virtual span");

	res_base = align_down(lo, granule);
	res_size = align_up(hi, granule) - res_base;
	out->base = res_base;
	out->size = res_size;

	/* No two segments of unlike protection may share a granule, because the
	 * protection pass cannot separate them on this host. Detected before a
	 * byte is reserved so the object is refused rather than half-placed. */
	for (i = 0; i < p->load_count; i++) {
		uint64_t vi = p->load[i].vaddr + bias;
		uint64_t gi0 = align_down(vi, granule);
		uint64_t gi1 = align_up(vi + p->load[i].memsz, granule);
		for (j = i + 1; j < p->load_count; j++) {
			uint64_t vj = p->load[j].vaddr + bias;
			uint64_t gj0 = align_down(vj, granule);
			uint64_t gj1 = align_up(vj + p->load[j].memsz, granule);
			if (gi0 < gj1 && gj0 < gi1 &&
			    prot_of(p->load[i].flags) != prot_of(p->load[j].flags))
				return fail(diag, elf_map_err_granule, "p_vaddr",
				    "PT_LOAD[%u] and PT_LOAD[%u] carry unlike "
				    "protection and share a 0x%llx granule",
				    i, j, (unsigned long long) granule);
		}
	}

	/* Reserve and commit the whole span, writable, in one region. Committed
	 * pages arrive zeroed, which is what makes .bss free; the copy pass
	 * relies on it and the assertion below proves it held. The base goes in
	 * as a bare hint, never MAP_FIXED: Cygwin 3.6.10 lets MAP_FIXED land on
	 * an already-reserved span without re-zeroing it
	 * (spike/map-and-jump/issue/0002), so occupancy is discriminated by the
	 * hint instead. A free span comes back at the asked-for base exactly; an
	 * occupied one comes back relocated, and the relocated region is
	 * unmapped and the object refused. */
	got = mmap((void *)(uintptr_t) res_base, (size_t) res_size,
	           PROT_READ | PROT_WRITE,
	           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (got == MAP_FAILED)
		return fail(diag, elf_map_err_reserve, "mmap",
		    "reserve of 0x%llx bytes at 0x%llx refused (errno %d)",
		    (unsigned long long) res_size,
		    (unsigned long long) res_base, errno);
	if ((uint64_t)(uintptr_t) got != res_base) {
		munmap(got, (size_t) res_size);
		return fail(diag, elf_map_err_reserve, "mmap",
		    "span of 0x%llx bytes at 0x%llx is occupied; the host "
		    "relocated the reserve to 0x%llx",
		    (unsigned long long) res_size,
		    (unsigned long long) res_base,
		    (unsigned long long)(uintptr_t) got);
	}

	/* Copy pass. Every segment's file bytes land at its runtime address; the
	 * memsz tail past filesz is left as the committed zero it arrived as. */
	for (i = 0; i < p->load_count; i++) {
		const elf_load_seg *s = &p->load[i];
		uint64_t v = s->vaddr + bias;
		if (s->filesz)
			memcpy((void *)(uintptr_t) v, image + s->off, (size_t) s->filesz);
		out->seg[i].vaddr = v;
		out->seg[i].filesz = s->filesz;
		out->seg[i].memsz = s->memsz;
		out->seg[i].flags = s->flags;
		out->seg[i].prot_lo = align_down(v, page);
		out->seg[i].prot_hi = align_up(v + s->memsz, page);
	}
	out->seg_count = p->load_count;

	/* The zero-fill assertion. A single non-zero byte past filesz means the
	 * host handed back a dirty page, which would silently corrupt every .bss
	 * that follows, so it is fatal rather than repaired. */
	for (i = 0; i < p->load_count; i++) {
		const elf_load_seg *s = &p->load[i];
		if (s->memsz > s->filesz) {
			const volatile unsigned char *tail =
			    (const volatile unsigned char *)(uintptr_t)(s->vaddr + bias + s->filesz);
			uint64_t k, n = s->memsz - s->filesz;
			if (n > page) n = page;   /* first page of the tail is enough */
			for (k = 0; k < n; k++)
				if (tail[k] != 0) {
					fail(diag, elf_map_err_bss, "p_memsz",
					    "segment %u byte 0x%llx past filesz was 0x%02x, "
					    "not zero", i, (unsigned long long)(s->filesz + k),
					    tail[k]);
					elf_unmap(out);
					return elf_map_err_bss;
				}
		}
	}

	/* Protection pass, granule by granule. Every granule the reservation
	 * covers is set to the protection of the one segment that owns it, or to
	 * no access when no segment does, so an inter-segment gap faults. The
	 * granule conflict check above guarantees at most one protection per
	 * granule. */
	{
		uint64_t a;
		for (a = res_base; a < res_base + res_size; a += granule) {
			int prot = PROT_NONE;
			for (i = 0; i < p->load_count; i++) {
				uint64_t vi = p->load[i].vaddr + bias;
				if (align_down(vi, granule) < a + granule &&
				    a < align_up(vi + p->load[i].memsz, granule)) {
					prot = prot_of(p->load[i].flags);
					break;
				}
			}
			if (mprotect((void *)(uintptr_t) a, (size_t) granule, prot) != 0) {
				fail(diag, elf_map_err_protect, "mprotect",
				    "granule at 0x%llx would not take protection %d "
				    "(errno %d)", (unsigned long long) a, prot, errno);
				elf_unmap(out);
				return elf_map_err_protect;
			}
		}
	}

	record_relro(p, bias, page, out);
	record_gnu_stack(image, image_size, p, out);
	return elf_map_ok;
}

elf_map_err elf_map_protect_relro(elf_mapping *m, elf_map_diag *diag)
{
	uint64_t a, lo, hi, granule;
	if (!m || !diag)
		return elf_map_err_arg;
	memset(diag, 0, sizeof *diag);
	if (!m->has_relro || m->relro_applied)
		return elf_map_ok;
	granule = m->granule;
	/* Freeze at granule resolution: the range snaps out to the granules it
	 * touches, which may cover a little more than the object marked, and is
	 * the finest the host allows. */
	lo = align_down(m->relro_lo, granule);
	hi = align_up(m->relro_hi, granule);
	for (a = lo; a < hi; a += granule)
		if (mprotect((void *)(uintptr_t) a, (size_t) granule, PROT_READ) != 0)
			return fail(diag, elf_map_err_protect, "mprotect",
			    "relro granule at 0x%llx would not go read-only (errno %d)",
			    (unsigned long long) a, errno);
	m->relro_applied = 1;
	return elf_map_ok;
}

void elf_unmap(elf_mapping *m)
{
	if (m && m->base && m->size)
		munmap((void *)(uintptr_t) m->base, (size_t) m->size);
	if (m)
		memset(m, 0, sizeof *m);
}
