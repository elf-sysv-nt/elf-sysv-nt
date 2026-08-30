/* WP-42: the three phases, the loader lock, and the region table.
 *
 * The bracket is POSIX's and the ordering is POSIX's: prepare handlers run in
 * the reverse of registration order, parent and child handlers in registration
 * order. The loader lock is taken after the last prepare handler and is
 * therefore the innermost thing held across the call, which is what makes a
 * fork from a thread inside dlopen safe: the forking thread cannot proceed
 * until whoever is in dlopen has left, and no thread can enter it afterwards.
 *
 * The child does not unlock. A mutex whose owner does not exist has no defined
 * unlock, and the child's only thread is not the thread that took it -- it is a
 * copy of it, which is a different thing to a mutex that recorded an owner.
 * child_reinit initializes over the lock instead, which is what glibc does at
 * the same point and for the same reason.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "fork.h"

static void set_why(elf_fork_state *fs, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static void set_why(elf_fork_state *fs, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(fs->why, sizeof fs->why, fmt, ap);
	va_end(ap);
}

void elf_fork_state_init(elf_fork_state *fs, dl_state *dl, elf_tls_state *tls,
                         const elf_fork_mem *mem, const elf_fork_lock *lock)
{
	memset(fs, 0, sizeof(*fs));
	fs->dl = dl;
	fs->tls = tls;
	fs->mem = mem != NULL ? *mem : *elf_fork_mem_host();
	fs->lock = lock != NULL ? *lock : *elf_fork_lock_pthread();
}

int elf_fork_atfork(elf_fork_state *fs, void (*prepare)(void),
                    void (*parent)(void), void (*child)(void))
{
	if (fs->handler_count >= ELF_FORK_ATFORK_MAX) {
		set_why(fs, "atfork table full at %u entries", ELF_FORK_ATFORK_MAX);
		return -1;
	}
	uint32_t i = fs->handler_count++;
	fs->handler[i].prepare = prepare;
	fs->handler[i].parent = parent;
	fs->handler[i].child = child;
	return 0;
}

/* ---- the region table -------------------------------------------------- */

/* Kept sorted by base and disjoint, because the unpacker requires both and
 * because the child replays in address order, which is the order in which a
 * refusal is cheapest to explain. */
int elf_fork_region_add(elf_fork_state *fs, uint64_t base, uint64_t size,
                        elf_fork_region_kind kind, uint32_t prot,
                        const char *what)
{
	if (size == 0) {
		set_why(fs, "a region of zero size cannot be reserved");
		return -1;
	}
	if (base > UINT64_MAX - size) {
		set_why(fs, "region at 0x%llx wraps the address space",
		        (unsigned long long)base);
		return -1;
	}
	if (fs->region_count >= ELF_FORK_REGION_MAX) {
		set_why(fs, "region table full at %u entries", ELF_FORK_REGION_MAX);
		return -1;
	}

	uint64_t end = base + size;
	uint32_t at = fs->region_count;
	for (uint32_t i = 0; i < fs->region_count; i++) {
		const elf_fork_region *r = &fs->region[i];
		if (base < r->base + r->size && r->base < end) {
			set_why(fs, "region 0x%llx+0x%llx overlaps %s at 0x%llx",
			        (unsigned long long)base, (unsigned long long)size,
			        r->what, (unsigned long long)r->base);
			return -1;
		}
		if (r->base > base && at == fs->region_count)
			at = i;
	}

	memmove(&fs->region[at + 1], &fs->region[at],
	        (fs->region_count - at) * sizeof fs->region[0]);

	elf_fork_region *r = &fs->region[at];
	memset(r, 0, sizeof(*r));
	r->base = base;
	r->size = size;
	r->kind = (uint32_t)kind;
	r->prot = prot;
	if (what != NULL) {
		size_t n = strlen(what);
		if (n >= ELF_FORK_WHAT_MAX)
			n = ELF_FORK_WHAT_MAX - 1;
		memcpy(r->what, what, n);
		r->what[n] = '\0';
	}
	fs->region_count++;
	return 0;
}

int elf_fork_region_drop(elf_fork_state *fs, uint64_t base)
{
	for (uint32_t i = 0; i < fs->region_count; i++) {
		if (fs->region[i].base != base)
			continue;
		memmove(&fs->region[i], &fs->region[i + 1],
		        (fs->region_count - i - 1) * sizeof fs->region[0]);
		fs->region_count--;
		return 0;
	}
	set_why(fs, "no region starts at 0x%llx", (unsigned long long)base);
	return -1;
}

/* ---- the phases --------------------------------------------------------- */

int elf_fork_prepare(elf_fork_state *fs, elf_fork_flavor flavor,
                     elfsysv_tcb_t *tcb)
{
	(void)flavor;

	if (fs->armed) {
		set_why(fs, "prepare called with a fork already armed");
		return -1;
	}

	/* This thread's, for this fork. The previous fork's TCB is not this
	 * thread's and may not exist any more. */
	fs->tcb = tcb;

	/* Reverse registration order: the handler registered last runs first, so a
	 * library that registered after the one it depends on quiesces first. */
	for (uint32_t i = fs->handler_count; i > 0; i--)
		if (fs->handler[i - 1].prepare != NULL)
			fs->handler[i - 1].prepare();

	if (fs->lock.acquire != NULL) {
		fs->lock.acquire(fs->lock.ctx);
		fs->locked = 1;
	}

	elf_fork_audit_take(fs, &fs->before);
	fs->armed = 1;
	fs->why[0] = '\0';
	return 0;
}

void elf_fork_parent(elf_fork_state *fs)
{
	if (fs->locked && fs->lock.release != NULL)
		fs->lock.release(fs->lock.ctx);
	fs->locked = 0;
	fs->armed = 0;

	for (uint32_t i = 0; i < fs->handler_count; i++)
		if (fs->handler[i].parent != NULL)
			fs->handler[i].parent();
}

int elf_fork_child(elf_fork_state *fs, const unsigned char *manifest, size_t len)
{
	/* First, unconditionally: the lock. Everything below may want it, and a
	 * lock that crossed held is the deadlock this package exists to prevent. */
	if (fs->lock.child_reinit != NULL)
		fs->lock.child_reinit(fs->lock.ctx);
	fs->locked = 0;
	fs->armed = 0;

	/* Then the address space, before any other allocation can land in it. */
	if (manifest != NULL && len > 0) {
		elf_fork_region got[ELF_FORK_REGION_MAX];
		char why[ELF_FORK_WHY_MAX];
		int n = elf_fork_manifest_unpack(manifest, len, got, why, sizeof why);
		if (n < 0) {
			set_why(fs, "manifest: %s", why);
			return -1;
		}

		for (int i = 0; i < n; i++) {
			const elf_fork_region *r = &got[i];
			if (fs->mem.reserve == NULL ||
			    fs->mem.reserve(fs->mem.ctx, r->base, r->size) != 0) {
				set_why(fs, "%s at 0x%llx refused in the child",
				        r->what, (unsigned long long)r->base);
				return -1;
			}
			if (r->kind == elf_fork_region_commit &&
			    (fs->mem.commit == NULL ||
			     fs->mem.commit(fs->mem.ctx, r->base, r->size, r->prot) != 0)) {
				set_why(fs, "%s at 0x%llx would not commit in the child",
				        r->what, (unsigned long long)r->base);
				return -1;
			}
		}

		fs->region_count = (uint32_t)n;
		memcpy(fs->region, got, (size_t)n * sizeof got[0]);
	}

	/* Then the thread pointer. The carrier is keyed to NtTib.StackBase and the
	 * child's initial thread has a different one, so the word the parent wrote
	 * is not the word this thread reads. WP-30 delivered the repair for here. */
	if (fs->tcb != NULL)
		elfsysv_tp_reestablish(fs->tcb);

	/* Only now is a comparison meaningful. */
	elf_fork_audit after;
	elf_fork_audit_take(fs, &after);

	char why[ELF_FORK_WHY_MAX];
	if (elf_fork_audit_diff(&fs->before, &after, why, sizeof why) != 0) {
		set_why(fs, "%s", why);
		return -1;
	}

	for (uint32_t i = 0; i < fs->handler_count; i++)
		if (fs->handler[i].child != NULL)
			fs->handler[i].child();

	fs->why[0] = '\0';
	return 0;
}

const elf_fork_audit *elf_fork_audit_before(const elf_fork_state *fs)
{
	return &fs->before;
}
