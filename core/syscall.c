/*
 * syscall.c -- write and exit_group, and nothing else yet.
 *
 * write for fd 1 is the load-bearing one: it copies the user buffer in through
 * the substrate's user_copy_in -- never a direct dereference of the user
 * pointer -- and emits the bytes to the host console.  A bad user address comes
 * back from the copy as a fault, which becomes -EFAULT, so a wild pointer in
 * user code cannot fault the kernel.  exit_group records the code, wakes the
 * main thread, and parks: the thread does not return to user space, the way a
 * real exit_group never does.
 */
#include "syscall.h"
#include "host.h"

/* el8 UAPI, confirmed against the target headers. */
#define NR_write	1
#define NR_exit_group	231

#define ENOSYS		38
#define EBADF		9
#define EFAULT		14

static long sys_write(struct substrate *s, long fd, uint64_t buf, long len)
{
	unsigned char k[4096];
	long done = 0;

	if (fd != 1 && fd != 2)
		return -EBADF;
	if (len < 0)
		return -EFAULT;

	while (done < len) {
		long chunk = len - done;
		uint64_t fault = 0;
		long w;

		if (chunk > (long)sizeof k)
			chunk = (long)sizeof k;
		if (s->user_copy_in(s, k, buf + done, (size_t)chunk, &fault) != 0)
			return done ? done : -EFAULT;
		w = host_console_write((int)fd, k, (size_t)chunk);
		if (w < 0)
			return done ? done : -EBADF;
		done += w;
		if (w < chunk)		/* a short host write ends it here */
			break;
	}
	return done;
}

static long sys_exit_group(long code)
{
	host_run_finish((int)code);
	host_park();			/* does not return */
	return 0;			/* unreachable */
}

long syscall_dispatch(struct substrate *s, const struct sysframe *f)
{
	switch (f->nr) {
	case NR_write:
		return sys_write(s, f->a1, (uint64_t)f->a2, f->a3);
	case NR_exit_group:
		return sys_exit_group(f->a1);
	default:
		return -ENOSYS;
	}
}
