/*
 * exec.h -- build the initial process image and enter it.
 *
 * This raises the x86-64 SysV initial stack the program's _start expects --
 * argc, argv, envp, the auxiliary vector -- on a stack mapped through the
 * substrate, and starts the thread at the entry with the auxv's AT_SYSINFO
 * carrying the gate the program will call.
 */
#ifndef CORE_EXEC_H
#define CORE_EXEC_H

#include <stdint.h>

#include "binfmt_elf.h"
#include "substrate.h"

/*
 * Build the initial stack for the loaded image li, publishing gate_addr as
 * AT_SYSINFO, and start thread tid at the entry.  argv is the argc-long vector
 * of argument strings; the environment is empty in this increment.  Returns 0
 * once the thread is running, or a negative value on failure.
 */
int exec_enter(struct substrate *s, const struct load_info *li,
	       uint64_t gate_addr, int argc, char **argv, int tid);

#endif /* CORE_EXEC_H */
