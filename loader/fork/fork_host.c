/* WP-42: the two host implementations, kept in one translation unit.
 *
 * <windows.h> is here and nowhere else in this package, the same separation
 * WP-32 keeps in host_mem.c: everything above reasons about ranges and
 * ordering, and only this file knows what a reservation is made of.
 *
 * The reservation is deliberately raw VirtualAlloc rather than mmap. A region
 * in this manifest is one the host's fork did not replay, and asking the host
 * to replay it in the child would put it back in the bookkeeping the parent
 * kept it out of, so a second fork would replay it twice. What the child needs
 * is the address space held, at that exact address, by nobody in particular.
 */
#include <pthread.h>

#include "fork.h"
#include "../elf/elf_types.h"   /* PF_R, PF_W, PF_X */

#if defined(__CYGWIN__) || defined(_WIN32)
#include <windows.h>

static DWORD win_prot(uint32_t pf)
{
	const int r = (pf & PF_R) != 0, w = (pf & PF_W) != 0, x = (pf & PF_X) != 0;
	if (x)
		return w ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
	if (w)
		return PAGE_READWRITE;
	if (r)
		return PAGE_READONLY;
	return PAGE_NOACCESS;
}

static int host_reserve(void *ctx, uint64_t base, uint64_t size)
{
	(void)ctx;
	void *got = VirtualAlloc((LPVOID)(uintptr_t)base, (SIZE_T)size,
	                         MEM_RESERVE, PAGE_NOACCESS);
	/* An exact address or nothing: a reservation the host satisfied somewhere
	 * else is worse than a refusal, because the caller would carry on with a
	 * window that no longer contains what the parent put in it. */
	if (got == NULL)
		return -1;
	if ((uint64_t)(uintptr_t)got != base) {
		VirtualFree(got, 0, MEM_RELEASE);
		return -1;
	}
	return 0;
}

static int host_commit(void *ctx, uint64_t base, uint64_t size, uint32_t prot)
{
	(void)ctx;
	void *got = VirtualAlloc((LPVOID)(uintptr_t)base, (SIZE_T)size,
	                         MEM_COMMIT, win_prot(prot));
	return got != NULL && (uint64_t)(uintptr_t)got == base ? 0 : -1;
}

static int host_release(void *ctx, uint64_t base, uint64_t size)
{
	(void)ctx;
	(void)size;   /* a Windows reservation is released whole or not at all */
	return VirtualFree((LPVOID)(uintptr_t)base, 0, MEM_RELEASE) ? 0 : -1;
}

#else /* a host without Win32: the package still builds and is still testable */

#include <sys/mman.h>

static int host_reserve(void *ctx, uint64_t base, uint64_t size)
{
	(void)ctx;
	void *got = mmap((void *)(uintptr_t)base, (size_t)size, PROT_NONE,
	                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	return got == (void *)(uintptr_t)base ? 0 : -1;
}

static int host_commit(void *ctx, uint64_t base, uint64_t size, uint32_t prot)
{
	(void)ctx;
	int p = 0;
	if (prot & PF_R) p |= PROT_READ;
	if (prot & PF_W) p |= PROT_WRITE;
	if (prot & PF_X) p |= PROT_EXEC;
	return mprotect((void *)(uintptr_t)base, (size_t)size, p);
}

static int host_release(void *ctx, uint64_t base, uint64_t size)
{
	(void)ctx;
	return munmap((void *)(uintptr_t)base, (size_t)size);
}

#endif

const elf_fork_mem *elf_fork_mem_host(void)
{
	static const elf_fork_mem m = {
		host_reserve, host_commit, host_release, NULL
	};
	return &m;
}

/* ---- the loader lock ---------------------------------------------------- */

/* Recursive, because dlopen calls into an initializer which may call dlopen,
 * and a prepare handler that runs on a thread already inside the loader would
 * otherwise deadlock on the way to a fork rather than in the child. */
static pthread_mutex_t loader_lock;
static int loader_lock_ready;

static void lock_make(void)
{
	pthread_mutexattr_t at;
	pthread_mutexattr_init(&at);
	pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&loader_lock, &at);
	pthread_mutexattr_destroy(&at);
	loader_lock_ready = 1;
}

static void lock_acquire(void *ctx)
{
	(void)ctx;
	if (!loader_lock_ready)
		lock_make();
	pthread_mutex_lock(&loader_lock);
}

static void lock_release(void *ctx)
{
	(void)ctx;
	if (loader_lock_ready)
		pthread_mutex_unlock(&loader_lock);
}

/* Initialize over it rather than unlock it. The child's thread is a copy of the
 * thread that took the lock, not that thread, and unlocking a recursive mutex
 * whose owner is a thread id that no longer names anything is not defined.
 * Re-initializing is: it leaves the lock unheld and owned by nobody, which is
 * the state the child needs and the state POSIX's own guidance describes. */
static void lock_child_reinit(void *ctx)
{
	(void)ctx;
	lock_make();
}

const elf_fork_lock *elf_fork_lock_pthread(void)
{
	static const elf_fork_lock l = {
		lock_acquire, lock_release, lock_child_reinit, NULL
	};
	return &l;
}
