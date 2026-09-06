/*
 * lxerrno.h -- the Linux errno values, the el8 UAPI's (asm-generic/errno.h),
 * for the core and the glue beneath it.
 *
 * mingw's errno.h assigns different numbers to several of the same names
 * (ENAMETOOLONG is 38 there and 36 on Linux; ENOSYS 40 against 38), and a
 * host header pulled in for an NT call brings it along.  A core that returned
 * the host's number for a Linux name would hand a program the wrong errno with
 * the right spelling, which no test that reads a symbol would catch.  So every
 * name is undefined and redefined here, and this header is included after any
 * host header, never before.
 */
#ifndef CORE_LXERRNO_H
#define CORE_LXERRNO_H


#undef EPERM
#undef ENOENT
#undef ESRCH
#undef EINTR
#undef EIO
#undef ENXIO
#undef E2BIG
#undef ENOEXEC
#undef EBADF
#undef ECHILD
#undef EAGAIN
#undef ENOMEM
#undef EACCES
#undef EFAULT
#undef EBUSY
#undef EEXIST
#undef EXDEV
#undef ENODEV
#undef ENOTDIR
#undef EISDIR
#undef EINVAL
#undef ENFILE
#undef EMFILE
#undef ENOTTY
#undef ETXTBSY
#undef EFBIG
#undef ENOSPC
#undef ESPIPE
#undef EROFS
#undef EMLINK
#undef EPIPE
#undef EDOM
#undef ERANGE
#undef EDEADLK
#undef ENAMETOOLONG
#undef ENOLCK
#undef ENOSYS
#undef ENOTEMPTY
#undef ELOOP
#undef EWOULDBLOCK
#undef ENOTSUP
#undef EOPNOTSUPP
#undef EOVERFLOW
#undef ENOTSOCK

#define EPERM		1
#define ENOENT		2
#define ESRCH		3
#define EINTR		4
#define EIO		5
#define ENXIO		6
#define E2BIG		7
#define ENOEXEC		8
#define EBADF		9
#define ECHILD		10
#define EAGAIN		11
#define ENOMEM		12
#define EACCES		13
#define EFAULT		14
#define EBUSY		16
#define EEXIST		17
#define EXDEV		18
#define ENODEV		19
#define ENOTDIR		20
#define EISDIR		21
#define EINVAL		22
#define ENFILE		23
#define EMFILE		24
#define ENOTTY		25
#define ETXTBSY		26
#define EFBIG		27
#define ENOSPC		28
#define ESPIPE		29
#define EROFS		30
#define EMLINK		31
#define EPIPE		32
#define EDOM		33
#define ERANGE		34
#define EDEADLK		35
#define ENAMETOOLONG	36
#define ENOLCK		37
#define ENOSYS		38
#define ENOTEMPTY	39
#define ELOOP		40
#define EWOULDBLOCK	EAGAIN
#define ENOTSOCK	88
#define EOPNOTSUPP	95
#define ENOTSUP		EOPNOTSUPP
#define EOVERFLOW	75

#endif /* CORE_LXERRNO_H */
