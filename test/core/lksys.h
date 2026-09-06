/*
 * lksys.h -- the syscall shim for the core's freestanding test programs.
 *
 * One source, two builds.  Under the core the program reaches the kernel
 * through the gate whose address _start found in AT_SYSINFO (N cannot trap
 * `syscall`; spike 1); under the Rocky 8 oracle, built with -DLK_ORACLE, the
 * same source uses the `syscall` instruction a real kernel traps.  Both share
 * the register convention: number in %rax, arguments in %rdi %rsi %rdx %r10
 * %r8 %r9, result in %rax, %rcx and %r11 clobbered.
 *
 * The gate is reached by a call, which pushes a return address below %rsp,
 * so the asm steps over the red zone first: the compiler may have a leaf
 * function's locals there, and a call in inline asm is invisible to it.
 */
#ifndef LKSYS_H
#define LKSYS_H

extern void *lk_gate;

static inline long lk_syscall6(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	register long r10 __asm__("r10") = a4;
	register long r8 __asm__("r8") = a5;
	register long r9 __asm__("r9") = a6;
	long ret;
#ifdef LK_ORACLE
	__asm__ volatile ("syscall"
		: "=a"(ret)
		: "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
		: "rcx", "r11", "memory");
#else
	register void *gate __asm__("r15") = lk_gate;
	__asm__ volatile ("leaq -128(%%rsp), %%rsp\n\tcall *%%r15\n\tleaq 128(%%rsp), %%rsp"
		: "=a"(ret)
		: "r"(gate), "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
		: "rcx", "r11", "memory");
#endif
	return ret;
}

#define lk_syscall0(n)			lk_syscall6(n, 0, 0, 0, 0, 0, 0)
#define lk_syscall1(n, a)		lk_syscall6(n, (long)(a), 0, 0, 0, 0, 0)
#define lk_syscall2(n, a, b)		lk_syscall6(n, (long)(a), (long)(b), 0, 0, 0, 0)
#define lk_syscall3(n, a, b, c)		lk_syscall6(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)
#define lk_syscall4(n, a, b, c, d)	lk_syscall6(n, (long)(a), (long)(b), (long)(c), (long)(d), 0, 0)
#define lk_syscall5(n, a, b, c, d, e)	lk_syscall6(n, (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), 0)

/* The program's entry, called by _start with the psABI's three. */
int lk_main(int argc, char **argv, char **envp);

#endif /* LKSYS_H */
