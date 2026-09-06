/*
 * task.c -- the one process of phase 2.
 */
#include <stdlib.h>
#include <string.h>

#include "task.h"
#include "lxerrno.h"

struct task *current;

static struct task the_task;

int task_init(const char *root_host_path)
{
	int r, i;
	memset(&the_task, 0, sizeof the_task);
	the_task.pid = 1;
	fdtable_init(&the_task.fdt);
	r = vfs_init(&the_task.fs, root_host_path);
	if (r) return r;
	current = &the_task;
	for (i = 0; i < 3; i++) {
		struct file *f = vfs_console_file(i);
		if (!f) return -ENOMEM;
		fd_install(&the_task.fdt, f, 0, i);
		file_put(f);
	}
	return 0;
}

void task_release(void)
{
	if (!current) return;
	fdtable_release(&current->fdt);
	vfs_release(&current->fs);
	current = NULL;
}

struct fs_ctx *vfs_current_fs(void)
{
	return &current->fs;
}

struct fdtable *vfs_current_fdtable(void)
{
	return &current->fdt;
}
