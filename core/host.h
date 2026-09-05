/*
 * host.h -- the host services the core stands on, named in Linux-neutral terms.
 *
 * lk-host is one mingw process: the Linux personality runs above the substrate
 * line, and a thin band of NT glue runs below it.  Everything the core needs
 * from the host that is not one of the substrate's nine calls lives here -- the
 * console it writes a foreign fd to, the kernel stack the gate switches onto,
 * and the lifecycle that lets exit_group end the run and the main thread read
 * its code back.  The core calls these; only host.c names the NT primitives
 * behind them, which is why the substrate-line check finds nothing above it.
 */
#ifndef CORE_HOST_H
#define CORE_HOST_H

#include <stddef.h>
#include <stdint.h>

/* Write len bytes to host fd (1 stdout, 2 stderr).  Returns the count written,
 * or -1 if the fd is not one the host backs.  This is the foreign-handle write
 * the plan's `write` syscall funnels fd 1 into. */
int64_t host_console_write(int fd, const void *buf, size_t len);

/* A kernel stack for the gate to switch onto, so the core's dispatch frames
 * never run on the user thread's small stack.  Returns a 16-aligned top (one
 * past the highest usable byte), or NULL on failure.  size is rounded up. */
void *host_alloc_kstack(size_t size);

/* The run lifecycle.  init once before the user thread starts; the user thread
 * calls finish from exit_group; the main thread waits and reads the code.
 * wait returns the recorded code, or -1 if timeout_ms elapses first. */
void host_run_init(void);
void host_run_finish(int code);
int  host_run_wait(unsigned timeout_ms);

/* Park the calling thread forever.  exit_group calls this after finish so the
 * user thread never returns to user code; the process is torn down when the
 * main thread returns the run's code. */
void host_park(void);

#endif /* CORE_HOST_H */
