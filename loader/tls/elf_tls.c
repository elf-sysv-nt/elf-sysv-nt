/* WP-37: TLS in the loader -- static block sizing, the DTV, __tls_get_addr,
 * and teardown. See elf_tls.h for the model and the WP-30/WP-34 seam.
 *
 * The static layout follows psABI variant II and mirrors WP-34's
 * assign_static_tls exactly for the initial set, so an initial-exec offset this
 * package hands out and a TPOFF64 WP-34 computes name the same byte. What this
 * package adds is the surplus below the initial modules, the DTV a thread reads
 * through tcbhead_t.dtv, the lazy allocation general-dynamic and local-dynamic
 * need for a module that is not in the static block, and the teardown.
 */
#define _GNU_SOURCE
#include "elf_tls.h"

#include <stdlib.h>
#include <string.h>

/* The state __tls_get_addr resolves against. Set by elf_tls_thread_install or
 * elf_tls_set_state; __tls_get_addr reads the calling thread's pointer through
 * carrier C3 and this to find the module table and generation. */
static elf_tls_state *g_state;

static uint64_t roundup_u64(uint64_t v, uint64_t a)
{
	if (a <= 1)
		return v;
	return (v + a - 1) & ~(a - 1);
}

/* ---- sizing the static layout ------------------------------------------ */

void elf_tls_state_init(elf_tls_state *s, uint64_t surplus)
{
	memset(s, 0, sizeof *s);
	s->surplus = surplus ? surplus : ELF_TLS_SURPLUS_DEFAULT;
	s->generation = 1;   /* generation 0 reads as "never reconciled" */
}

uint64_t elf_tls_add_initial(elf_tls_state *s, const elf_tls_module *m)
{
	uint64_t id, align, size, running;
	elf_tls_module *slot;

	if (s->nmod >= ELF_TLS_MAX_MOD)
		return 0;
	id = ++s->nmod;
	slot = &s->mod[id];
	*slot = *m;
	slot->is_static = 1;

	/* Variant II, WP-34's rule: round the running extent up to the module's
	 * alignment after adding its size, so the block sits at aligned negative
	 * offsets below the pointer, the first module nearest. */
	align = m->align ? m->align : 1;
	size = m->mem_size;
	running = s->static_used + size;
	running = roundup_u64(running, align);
	slot->tpoff = -(int64_t) running;
	s->static_used = running;
	if (align > s->max_align)
		s->max_align = align;
	return id;
}

void elf_tls_finish_layout(elf_tls_state *s)
{
	uint64_t a = s->max_align < 64 ? 64 : s->max_align;
	s->static_size = roundup_u64(s->static_used + s->surplus, a);
}

uint64_t elf_tls_add_dynamic(elf_tls_state *s, const elf_tls_module *m)
{
	uint64_t id;

	if (s->nmod >= ELF_TLS_MAX_MOD)
		return 0;
	id = ++s->nmod;
	s->mod[id] = *m;
	s->mod[id].is_static = 0;
	s->mod[id].tpoff = 0;
	s->generation++;
	return id;
}

int64_t elf_tls_static_tpoff(const elf_tls_state *s, uint64_t modid)
{
	if (modid == 0 || modid > s->nmod || !s->mod[modid].is_static)
		return 0;
	return s->mod[modid].tpoff;
}

/* ---- the DTV -----------------------------------------------------------
 *
 * A DTV is an array of elf_tls_dtv. It is allocated with two extra entries so
 * glibc's layout holds: base[0] is the length word (dtv[-1]), base[1] is the
 * generation word (dtv[0]), and base[2..] are the one-based module slots. The
 * pointer returned and stored in tcbhead_t.dtv is &base[1], so dtv[i] indexes
 * module i, dtv[0] is the generation, and dtv[-1] is the length.
 */
static elf_tls_dtv *dtv_alloc(uint64_t nmods)
{
	elf_tls_dtv *base = calloc(nmods + 2, sizeof *base);
	if (!base)
		return NULL;
	base[0].counter = nmods;          /* dtv[-1]: module-slot count */
	return base + 1;                  /* dtv[0] onward */
}

static uint64_t dtv_len(elf_tls_dtv *dtv) { return dtv[-1].counter; }

/* Fill slots lo+1..hi for the given thread pointer: a static module points into
 * the TCB at tp + tpoff; a dynamic module is left unallocated. */
static void dtv_fill(elf_tls_state *s, void *tp, elf_tls_dtv *dtv,
		     uint64_t lo, uint64_t hi)
{
	uint64_t i;
	for (i = lo + 1; i <= hi; i++) {
		if (i <= s->nmod && s->mod[i].is_static) {
			dtv[i].pointer.val = (char *)tp + s->mod[i].tpoff;
			dtv[i].pointer.is_static = 1;
		} else {
			dtv[i].pointer.val = ELF_TLS_DTV_UNALLOCATED;
			dtv[i].pointer.is_static = 0;
		}
	}
}

/* Allocate and initialize a dynamic module's block: mem_size bytes aligned to
 * the module's alignment, init_size copied from the image, the rest zero. */
static void *block_alloc(const elf_tls_module *m)
{
	void *p = NULL;
	uint64_t align = m->align ? m->align : 16;
	size_t size = m->mem_size ? m->mem_size : 1;

	if (align < 16)
		align = 16;                 /* posix_memalign wants >= sizeof(void*) */
	size = roundup_u64(size, align);
	if (posix_memalign(&p, align, size) != 0)
		return NULL;
	memset(p, 0, size);
	if (m->init_size && m->init_image)
		memcpy(p, m->init_image, m->init_size);
	return p;
}

/* ---- per-thread install ------------------------------------------------ */

void elf_tls_set_state(elf_tls_state *s) { g_state = s; }

int elf_tls_thread_install(elf_tls_state *s, elfsysv_tcb_t *tcb)
{
	void *tp = tcb->tp;
	elf_tls_dtv *dtv;
	uint64_t i;

	/* Lay the initial modules' images into the static block below the head.
	 * The block was zeroed by elfsysv_tp_alloc's calloc, so only the init
	 * image needs copying; the .tbss tail is already zero. */
	for (i = 1; i <= s->nmod; i++) {
		const elf_tls_module *m = &s->mod[i];
		if (!m->is_static)
			continue;
		if (m->init_size && m->init_image)
			memcpy((char *)tp + m->tpoff, m->init_image, m->init_size);
	}

	dtv = dtv_alloc(s->nmod);
	if (!dtv)
		return -1;
	dtv[0].counter = s->generation;
	dtv_fill(s, tp, dtv, 0, s->nmod);

	tcb->head->dtv = dtv;
	g_state = s;
	return 0;
}

/* ---- the resolver ------------------------------------------------------ */

/* Reconcile a thread's DTV to the current generation: grow it if modules were
 * registered since it was built, fill the new slots, and stamp the generation.
 * Returns the (possibly moved) DTV and writes it back into the TCB head. */
static elf_tls_dtv *dtv_reconcile(elf_tls_state *s, void *tp, elf_tls_dtv *dtv)
{
	uint64_t oldlen = dtv_len(dtv);

	if (s->nmod > oldlen) {
		elf_tls_dtv *base = dtv - 1;
		elf_tls_dtv *nb = realloc(base, (s->nmod + 2) * sizeof *nb);
		if (!nb)
			return NULL;                 /* keep the old DTV; caller fails */
		dtv = nb + 1;
		dtv[-1].counter = s->nmod;
		dtv_fill(s, tp, dtv, oldlen, s->nmod);
		((elfsysv_tcbhead_t *)tp)->dtv = dtv;
	}
	dtv[0].counter = s->generation;
	return dtv;
}

void *elf_tls_get_addr_at(elf_tls_state *s, void *tp, const elf_tls_index *ti)
{
	elfsysv_tcbhead_t *head = (elfsysv_tcbhead_t *)tp;
	elf_tls_dtv *dtv = (elf_tls_dtv *)head->dtv;
	uint64_t mod = ti->ti_module;
	elf_tls_dtv *slot;

	if (!dtv)
		return NULL;
	if (dtv[0].counter != s->generation) {
		dtv = dtv_reconcile(s, tp, dtv);
		if (!dtv)
			return NULL;
	}
	if (mod == 0 || mod > dtv_len(dtv))
		return NULL;

	slot = &dtv[mod];
	if (slot->pointer.val == ELF_TLS_DTV_UNALLOCATED) {
		void *block = block_alloc(&s->mod[mod]);
		if (!block)
			return NULL;
		slot->pointer.val = block;
		slot->pointer.is_static = 0;
	}
	return (char *)slot->pointer.val + ti->ti_offset;
}

void *__tls_get_addr(const elf_tls_index *ti)
{
	return elf_tls_get_addr_at(g_state, elfsysv_tp_get(), ti);
}

/* ---- teardown ---------------------------------------------------------- */

void elf_tls_thread_teardown(elfsysv_tcb_t *tcb)
{
	elf_tls_dtv *dtv = (elf_tls_dtv *)tcb->head->dtv;
	uint64_t i, len;

	if (!dtv)
		return;
	len = dtv_len(dtv);
	for (i = 1; i <= len; i++) {
		void *v = dtv[i].pointer.val;
		if (!dtv[i].pointer.is_static && v != ELF_TLS_DTV_UNALLOCATED && v)
			free(v);
	}
	free(dtv - 1);
	tcb->head->dtv = NULL;
}
