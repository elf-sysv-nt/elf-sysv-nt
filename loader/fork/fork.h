/* WP-42: fork, vfork and posix_spawn.
 *
 * Cygwin's fork copies a process by creating a suspended child of the same
 * image, writing the parent's memory into it, and letting it resume at the
 * point of the call. Everything the loader keeps in its own writable data
 * crosses on that copy for free. Three things do not, and this package is the
 * three.
 *
 * The first is address space the loader took without going through the host's
 * memory bookkeeping. WP-32 maps an object through the host's mmap precisely so
 * the copier replays it; WP-41's window is a bare reservation, made by the
 * parent into a suspended child and re-reserved in pieces by elf_window_yield,
 * and nothing records it. A child that resumes without those reservations has a
 * hole where the ELF world lives, and the first host allocation to land in it
 * is unrecoverable. So the loader keeps a manifest of what it reserved outside
 * the host's view, the manifest is packed into bytes the child reads, and the
 * child replays it before anything else runs.
 *
 * The second is the thread pointer. DR-0003's carrier is keyed to
 * NtTib.StackBase, and the child's initial thread is not the parent's thread:
 * its stack base is somewhere else, so the carrier word is somewhere else and
 * holds whatever the copy put there. elfsysv_tp_reestablish writes the pointer
 * back for the calling thread, which is why WP-30 delivered it; the child path
 * is its only caller.
 *
 * The third is the loader lock. A mutex held by a thread that does not exist in
 * the child is held forever, and a fork from a thread that is inside dlopen is
 * exactly the case that produces one. The bracket is POSIX's, and this package
 * owns both halves of it: user prepare handlers run in reverse registration
 * order, then the loader lock is taken, and it is the innermost thing held
 * across the call. The parent releases it and runs parent handlers in
 * registration order. The child does not release it -- releasing a mutex whose
 * owner is gone is not defined -- it reinitializes it to unheld, repairs the
 * loader, and then runs child handlers.
 *
 * What crosses intact is checkable rather than asserted. elf_fork_audit_take
 * reduces the loader's state to one comparable record -- the object list and
 * every object's bias and dynamic section, the search configuration, the static
 * TLS layout and this thread's whole DTV, the r_debug structure and its address
 * -- and the child takes the same record and diffs it. The diff names the field
 * that moved, and one of the fields is the loader's own code address, which is
 * how the DLL rebase failure mode that haunts Cygwin's fork is confirmed absent
 * rather than assumed absent: a rebase moves it, and the child says so.
 *
 * vfork and posix_spawn take this path unchanged. Cygwin's vfork is a fork that
 * promises less, and posix_spawn is a fork followed by WP-41's exec branch;
 * neither has state of its own to cross. The flavour is carried only so a
 * diagnostic can name the call the operator made.
 */
#ifndef ELFSYSV_LOADER_FORK_H
#define ELFSYSV_LOADER_FORK_H

#include <stddef.h>
#include <stdint.h>

#include "../dl/dl.h"
#include "../tls/elf_tls.h"
#include "../rdebug/rdebug.h"

#ifdef __cplusplus
extern "C" {
#endif

/* POSIX requires at least _POSIX_THREAD_ATFORK entries and every practical
 * implementation is bounded; a fixed table keeps the child path allocation
 * free, which matters because the child path runs before the loader has been
 * repaired and must not depend on it. */
#define ELF_FORK_ATFORK_MAX 64

/* The most reservations the manifest carries. WP-41's yield leaves at most two
 * remainders per placed object and the window is placed once, so the real
 * number is small; the bound exists so the unpacker has one to check against. */
#define ELF_FORK_REGION_MAX 64

#define ELF_FORK_WHAT_MAX 24
#define ELF_FORK_WHY_MAX 160

/* Which call the operator made. Carried for diagnostics only: all three take
 * the same three phases in the same order. */
typedef enum {
	elf_fork_flavor_fork = 0,
	elf_fork_flavor_vfork,
	elf_fork_flavor_posix_spawn
} elf_fork_flavor;

/* ---- the manifest of reservations the host does not know about --------- */

typedef enum {
	elf_fork_region_reserve = 0, /* address space held, nothing committed */
	elf_fork_region_commit  = 1  /* held and committed; contents cross by copy */
} elf_fork_region_kind;

typedef struct {
	uint64_t base;                    /* exact address; the child fails without it */
	uint64_t size;                    /* bytes, nonzero, base + size does not wrap */
	uint32_t kind;                    /* elf_fork_region_kind */
	uint32_t prot;                    /* elf_map's protection encoding, commit only */
	char     what[ELF_FORK_WHAT_MAX]; /* who took it, NUL-terminated, for diagnostics */
} elf_fork_region;

/* How the child reclaims a region. The package makes no host call itself, the
 * same way WP-38 takes its file reads through a callback: reserve() must give
 * back exactly `base` or fail, commit() must commit within a region already
 * reserved, and free() is the teardown the parent uses when a region is
 * released. ctx is passed through untouched. elf_fork_mem_host() returns the
 * Win32 implementation, which lives in its own translation unit so <windows.h>
 * stays out of the rest of the package. */
typedef struct elf_fork_mem {
	int (*reserve)(void *ctx, uint64_t base, uint64_t size);
	int (*commit)(void *ctx, uint64_t base, uint64_t size, uint32_t prot);
	int (*release)(void *ctx, uint64_t base, uint64_t size);
	void *ctx;
} elf_fork_mem;

const elf_fork_mem *elf_fork_mem_host(void);

/* How the loader lock is taken. Separated from the memory host because the two
 * have nothing to do with each other and a test replaces one without the other.
 * child_reinit() must leave the lock unheld and owned by nobody, which on a
 * pthread mutex means initializing over it rather than unlocking it.
 * elf_fork_lock_pthread() returns the pthread implementation. */
typedef struct elf_fork_lock {
	void (*acquire)(void *ctx);
	void (*release)(void *ctx);
	void (*child_reinit)(void *ctx);
	void *ctx;
} elf_fork_lock;

const elf_fork_lock *elf_fork_lock_pthread(void);

/* ---- the audit: what crossing intact means ----------------------------- */

/* One comparable record of the loader's state. Every field is either an
 * address that must not move or a hash over a structure that must not change.
 * The hashes are FNV-1a over a canonical byte serialization of the fields named
 * in each comment, so a diff can say which structure moved without carrying a
 * copy of it. */
typedef struct {
	uint64_t magic;
	uint64_t self_addr;      /* the loader's own code: the rebase probe */
	uint64_t rdebug_addr;    /* &_r_debug */
	uint64_t rdebug_map;     /* _r_debug.r_map, the head node */
	uint32_t rdebug_version;
	uint32_t rdebug_state;
	uint64_t map_hash;       /* over every node's l_addr, l_ld and l_name */
	uint32_t map_len;        /* nodes walked from r_map, bounded */
	uint32_t obj_count;
	uint64_t obj_hash;       /* over each object's slot, soname, bias, dyn, refcount */
	uint64_t search_hash;    /* over the DT_NEEDED search configuration */
	uint64_t tls_static_size;
	uint64_t tls_generation;
	uint32_t tls_nmod;
	uint32_t dtv_len;
	uint64_t dtv_hash;       /* over this thread's DTV slots and their kinds */
	uint64_t tp;             /* this thread's pointer */
	uint32_t region_count;
	uint32_t pad;
	uint64_t region_hash;    /* over the manifest */
} elf_fork_audit;

#define ELF_FORK_AUDIT_MAGIC UINT64_C(0x5750343241554449) /* "WP42AUDI" */

/* The longest link_map chain the audit walks. A chain longer than this is a
 * defect in the map rather than a program with that many objects, and the audit
 * stops rather than following a cycle. */
#define ELF_FORK_MAP_WALK_MAX 512

/* ---- the state ---------------------------------------------------------- */

/* What the fork path is bound to. Any of the three may be NULL, which drops the
 * corresponding audit fields to zero on both sides and so still compares equal;
 * that is what lets a unit test drive the phases without a loaded object. */
typedef struct elf_fork_state {
	dl_state      *dl;
	elf_tls_state *tls;
	elfsysv_tcb_t *tcb;      /* the calling thread's TCB, re-established in the child */

	elf_fork_mem   mem;
	elf_fork_lock  lock;

	elf_fork_region region[ELF_FORK_REGION_MAX];
	uint32_t        region_count;

	struct {
		void (*prepare)(void);
		void (*parent)(void);
		void (*child)(void);
	} handler[ELF_FORK_ATFORK_MAX];
	uint32_t handler_count;

	elf_fork_audit before;   /* taken in prepare, read by the child */
	int            armed;    /* prepare ran and the lock is held */
	int            locked;

	char why[ELF_FORK_WHY_MAX]; /* the last failure, whichever side produced it */
} elf_fork_state;

/* Bring a state up empty and bind it. mem may be NULL, which installs
 * elf_fork_mem_host(); lock may be NULL, which installs the pthread one. */
void elf_fork_state_init(elf_fork_state *fs, dl_state *dl, elf_tls_state *tls,
                         elfsysv_tcb_t *tcb,
                         const elf_fork_mem *mem, const elf_fork_lock *lock);

/* Bind the TCB after the fact, for a thread that acquired one later. */
void elf_fork_bind_tcb(elf_fork_state *fs, elfsysv_tcb_t *tcb);

/* ---- pthread_atfork ----------------------------------------------------- */

/* Register a handler triple; any of the three may be NULL. Returns 0, or -1
 * with the reason in fs->why when the table is full. The ordering POSIX
 * specifies is the point: prepare handlers run in the reverse of registration
 * order, parent and child handlers in registration order. */
int elf_fork_atfork(elf_fork_state *fs, void (*prepare)(void),
                    void (*parent)(void), void (*child)(void));

/* ---- the manifest ------------------------------------------------------- */

/* Record a reservation the host's bookkeeping does not carry. `what` is
 * truncated to ELF_FORK_WHAT_MAX - 1 characters. Returns 0, or -1 with the
 * reason in fs->why: a zero size, a range that wraps, a range that overlaps one
 * already recorded, or a full table. */
int elf_fork_region_add(elf_fork_state *fs, uint64_t base, uint64_t size,
                        elf_fork_region_kind kind, uint32_t prot,
                        const char *what);

/* Forget a reservation, by base. Returns 0, or -1 when no region starts there. */
int elf_fork_region_drop(elf_fork_state *fs, uint64_t base);

/* Serialize the manifest into `buf`. Writes the byte count to *used. Returns 0,
 * or -1 when cap is too small. The encoding is little-endian and fixed-width,
 * because the writer and the reader are the same build in the same process
 * family and a self-describing format would only add cases to validate. */
int elf_fork_manifest_pack(const elf_fork_state *fs, unsigned char *buf,
                           size_t cap, size_t *used);

/* The bytes one manifest of `n` regions occupies. */
size_t elf_fork_manifest_size(uint32_t n);

/* Parse a manifest. This is the one place in the package that reads bytes it
 * did not write in the same call, so it is written as though it did not: every
 * field is bounds-checked, the count is checked against the buffer length
 * before it is used to index, each range is checked for a zero size and for
 * wrap, the ranges must be sorted ascending and must not overlap, each `what`
 * must be NUL-terminated within its field, and the buffer must end exactly
 * where the last region does. Returns the number of regions on success and
 * fills `out`; returns -1 on any of the above with the reason in `why`, which
 * may be NULL. `out` must have room for ELF_FORK_REGION_MAX. */
int elf_fork_manifest_unpack(const unsigned char *buf, size_t len,
                             elf_fork_region *out, char *why, size_t why_cap);

/* ---- the audit ---------------------------------------------------------- */

/* Reduce the bound state to one record. Reads the DTV through the bound TCB, so
 * it is the calling thread's DTV that is audited; that is the one the child
 * inherits. */
void elf_fork_audit_take(const elf_fork_state *fs, elf_fork_audit *a);

/* Compare two records. Returns 0 when they agree, or -1 with the name of the
 * first field that differs and both values written into `why`. The field order
 * is chosen so the most explanatory difference is reported first: a moved
 * loader image explains every other difference and is checked before them. */
int elf_fork_audit_diff(const elf_fork_audit *before, const elf_fork_audit *after,
                        char *why, size_t why_cap);

/* ---- the three phases --------------------------------------------------- */

/* Before the host's fork, on the calling thread: run prepare handlers in
 * reverse registration order, take the loader lock, and take the audit. After
 * this returns the loader is quiescent and no other thread can enter dlopen.
 * Returns 0; -1 only when the state is already armed, which is a caller error
 * and not a condition to recover from. */
int elf_fork_prepare(elf_fork_state *fs, elf_fork_flavor flavor);

/* In the parent, after the host's fork returns, whether it succeeded or not:
 * release the loader lock, then run parent handlers in registration order. */
void elf_fork_parent(elf_fork_state *fs);

/* In the child, as the first loader code it runs. In order: reinitialize the
 * loader lock to unheld, replay every region in `manifest` (which may be NULL
 * with len 0 when the parent packed none), re-establish the thread pointer from
 * the bound TCB, take the audit and diff it against the parent's, and then run
 * child handlers in registration order. Returns 0, or -1 with the reason in
 * fs->why -- a manifest that will not parse, a reservation the host refused, or
 * an audit field that moved. The child handlers do not run on failure, because
 * a handler that calls dlsym against a loader that did not cross is worse than
 * no handler at all. */
int elf_fork_child(elf_fork_state *fs, const unsigned char *manifest, size_t len);

/* The audit the parent took, for a child that wants to report the difference
 * rather than only fail on it. Valid between prepare and the next prepare. */
const elf_fork_audit *elf_fork_audit_before(const elf_fork_state *fs);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_FORK_H */
