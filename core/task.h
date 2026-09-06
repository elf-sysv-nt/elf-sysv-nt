/*
 * task.h -- the process, as far as this phase has one.
 *
 * Phase 2 runs one Linux process: its descriptor table and its file-system
 * context (mounts, working directory, umask, credentials) live here as
 * `current`.  Phase 3's fork clones the structure; nothing here assumes
 * there is only ever one, only that there is one running now.
 */
#ifndef CORE_TASK_H
#define CORE_TASK_H

#include "file.h"
#include "vfs.h"

struct task {
	int pid;
	struct fdtable fdt;
	struct fs_ctx fs;
};

extern struct task *current;

/* Bring up the one process over the root tree at root_host_path, with the
 * console on descriptors 0, 1 and 2.  0 or -errno. */
int task_init(const char *root_host_path);
void task_release(void);

#endif /* CORE_TASK_H */
