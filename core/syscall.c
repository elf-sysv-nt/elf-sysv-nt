/*
 * syscall.c -- the table the gate dispatches into.
 *
 * Phase 2 answers the file syscalls through sys_fs.c, where every user
 * address is copied across through the substrate and never dereferenced;
 * exit_group ends the run here.  Every other number returns -ENOSYS, which is
 * what a 4.18 kernel does for what it lacks.
 */
#include "syscall.h"
#include "sys_fs.h"
#include "host.h"
#include "lxerrno.h"
#include "lxtypes.h"

static int64_t sys_exit_group(int64_t code)
{
	host_run_finish((int)code);
	host_park();			/* does not return */
	return 0;			/* unreachable */
}

int64_t syscall_dispatch(struct substrate *s, const struct sysframe *f)
{
	int handled = 0;
	int64_t r;
	if (f->nr == NR_exit_group)
		return sys_exit_group(f->a1);
	r = sys_fs_dispatch(s, f, &handled);
	if (handled)
		return r;
	return -ENOSYS;
}
