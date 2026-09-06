/*
 * file.c -- open file descriptions and the descriptor table.
 *
 * The table is an array because RLIMIT_NOFILE bounds it and Linux's own is
 * one; lowest-free allocation is a linear scan from min, which is what the
 * rule costs.  A description dies on its last reference and its kind's
 * release runs then, so a dup'd descriptor closed early leaves the file open,
 * which is the whole reason descriptions and descriptors are two things.
 */
#include <stdlib.h>
#include <string.h>

#include "file.h"
#include "lxerrno.h"

struct file *file_new(const struct file_ops *ops, int kind, unsigned flags)
{
	struct file *f = calloc(1, sizeof *f);
	if (!f) return NULL;
	f->ops = ops;
	f->refs = 1;
	f->kind = kind;
	f->flags = flags;
	return f;
}

void file_get(struct file *f)
{
	f->refs++;
}

void file_put(struct file *f)
{
	if (!f) return;
	if (--f->refs > 0) return;
	if (f->ops && f->ops->release) f->ops->release(f);
	free(f);
}

void fdtable_init(struct fdtable *t)
{
	memset(t, 0, sizeof *t);
}

void fdtable_release(struct fdtable *t)
{
	int i;
	for (i = 0; i < NR_OPEN_DEFAULT; i++) {
		if (t->files[i]) {
			file_put(t->files[i]);
			t->files[i] = NULL;
		}
	}
}

int fd_install(struct fdtable *t, struct file *f, int cloexec, int min)
{
	int i;
	if (min < 0) return -EINVAL;
	for (i = min; i < NR_OPEN_DEFAULT; i++) {
		if (!t->files[i]) {
			file_get(f);
			t->files[i] = f;
			t->cloexec[i] = (unsigned char)(cloexec ? 1 : 0);
			return i;
		}
	}
	return -EMFILE;
}

struct file *fd_get(struct fdtable *t, int fd)
{
	if (fd < 0 || fd >= NR_OPEN_DEFAULT) return NULL;
	return t->files[fd];
}

int fd_close(struct fdtable *t, int fd)
{
	struct file *f = fd_get(t, fd);
	if (!f) return -EBADF;
	t->files[fd] = NULL;
	t->cloexec[fd] = 0;
	file_put(f);
	return 0;
}

int fd_dup2(struct fdtable *t, int oldfd, int newfd, int cloexec)
{
	struct file *f = fd_get(t, oldfd);
	if (!f) return -EBADF;
	if (newfd < 0 || newfd >= NR_OPEN_DEFAULT) return -EBADF;
	if (oldfd == newfd) return newfd;
	file_get(f);
	if (t->files[newfd]) file_put(t->files[newfd]);
	t->files[newfd] = f;
	t->cloexec[newfd] = (unsigned char)(cloexec ? 1 : 0);
	return newfd;
}

int fd_get_cloexec(struct fdtable *t, int fd)
{
	if (!fd_get(t, fd)) return -EBADF;
	return t->cloexec[fd] ? FD_CLOEXEC : 0;
}

int fd_set_cloexec(struct fdtable *t, int fd, int on)
{
	if (!fd_get(t, fd)) return -EBADF;
	t->cloexec[fd] = (unsigned char)(on ? 1 : 0);
	return 0;
}
