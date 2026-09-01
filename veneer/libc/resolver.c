/* The veneer's one hidden run-time resolver, shared by every generated body.
 *
 * WP-56, the reent-tls-bringup rung, item 2. generate.py emits, for each
 * FUNC/IFUNC row, a versioned body that names its elfsysv1.dll export as a
 * .rodata string and reaches it at RUN time -- not as an ELF symbol, because
 * the export lives in the PE export directory, which the ELF dynamic-symbol
 * table cannot name (spike/reent-veneer-body). This file is the machinery those
 * bodies share: the walk that turns a name into the face export's address, the
 * constructor that finds the faced runtime's base, and the cold trampoline a
 * FUNC body tail-calls so a single private copy does the work.
 *
 * Everything here is hidden. One veneer carries one resolver; no faced symbol
 * can collide with it, and none of it appears in .dynsym
 * (spike/reent-veneer-thunk's resolver_stays_private fact).
 *
 * The walk is runtime/face/t/elfcall.c's pe_export, ported and kept hidden.
 * It is freestanding: no libc call, so `ld -shared` links this beside the
 * generated symbols object with no startup files, exactly as build-libc links.
 */
#include <stdint.h>

#define AT_BASE 7

/* The faced runtime's mapped base, filled once by the constructor below.
 * Hidden: it is the resolver's own state, never a faced symbol. */
__attribute__((visibility("hidden"))) const uint8_t *_elfsysv_face_base;

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
	return p[0] | ((uint32_t)p[1] << 8)
	     | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int name_is(const uint8_t *p, const char *w)
{
	while (*w && *p == (uint8_t)*w) { p++; w++; }
	return *w == 0 && *p == 0;
}

/* Resolve one PE export by name against the faced runtime -- elfcall.c's walk.
 * Returns 0 when the base is unset or the name is absent; a real body faults on
 * the null tail-call, which is the honest failure for an unresolved export. */
__attribute__((visibility("hidden")))
void *_elfsysv_resolve(const char *name)
{
	const uint8_t *base = _elfsysv_face_base;
	const uint8_t *opt, *dir;
	uint32_t lfanew, nn, i;

	if (!base || rd16(base) != 0x5A4D)          /* "MZ" */
		return 0;
	lfanew = rd32(base + 0x3C);
	if (rd32(base + lfanew) != 0x00004550)      /* "PE\0\0" */
		return 0;
	opt = base + lfanew + 4 + 20;
	if (rd16(opt) != 0x20B)                     /* PE32+ */
		return 0;
	if (rd32(opt + 108) < 1 || rd32(opt + 112) == 0)
		return 0;                           /* no export directory */
	dir = base + rd32(opt + 112);
	nn = rd32(dir + 24);                        /* NumberOfNames */
	for (i = 0; i < nn; i++) {
		if (name_is(base + rd32(base + rd32(dir + 32) + 4u * i), name)) {
			uint16_t ord = rd16(base + rd32(dir + 36) + 2u * i);
			return (void *)(base + rd32(base + rd32(dir + 28) + 4u * ord));
		}
	}
	return 0;
}

/* DT_INIT_ARRAY constructor: walk auxv to AT_BASE once, before any body runs.
 * The loader's dyn-init runs this in WP-33 load order (the init-chain rung),
 * with (argc, argv, envp) the way glibc calls .init_array entries. */
__attribute__((constructor))
static void _elfsysv_face_init(int argc, char **argv, char **envp)
{
	uint64_t *p = (uint64_t *)envp;
	(void)argc; (void)argv;

	while (*p) p++;                             /* past envp */
	p++;
	for (; p[0]; p += 2)
		if (p[0] == AT_BASE) {
			_elfsysv_face_base = (const uint8_t *)(uintptr_t)p[1];
			break;
		}
}

/* The cold trampoline every FUNC body tail-jumps to. It receives the body's
 * own .rodata name pointer in %r11 -- generate.py's `lea name(%rip), %r11; jmp
 * _elfsysv_thunk` -- preserves the SysV integer and FP argument registers
 * around the resolve call, then tail-jumps to the faced export with the
 * original arguments intact. Naked asm: the body has no C prologue to clobber
 * the argument registers this trampoline exists to keep.
 *
 * Alignment: reached by `jmp`, so at entry %rsp is 8 mod 16 (a return address
 * from the body's own caller on the stack). Seven pushes bring %rsp to 0 mod 16;
 * the 128-byte save area keeps it there, so `call _elfsysv_resolve` meets the
 * ABI's 16-byte boundary and `movdqu` has an aligned slot. */
__attribute__((visibility("hidden")))
void _elfsysv_thunk(void);

__asm__(
"	.text\n"
"	.p2align 4\n"
"	.globl	_elfsysv_thunk\n"
"	.hidden	_elfsysv_thunk\n"
"	.type	_elfsysv_thunk, @function\n"
"_elfsysv_thunk:\n"
"	push	%rdi\n"
"	push	%rsi\n"
"	push	%rdx\n"
"	push	%rcx\n"
"	push	%r8\n"
"	push	%r9\n"
"	push	%rax\n"			/* varargs FP count; also the odd push that re-aligns */
"	sub	$128, %rsp\n"
"	movdqu	%xmm0, 0(%rsp)\n"
"	movdqu	%xmm1, 16(%rsp)\n"
"	movdqu	%xmm2, 32(%rsp)\n"
"	movdqu	%xmm3, 48(%rsp)\n"
"	movdqu	%xmm4, 64(%rsp)\n"
"	movdqu	%xmm5, 80(%rsp)\n"
"	movdqu	%xmm6, 96(%rsp)\n"
"	movdqu	%xmm7, 112(%rsp)\n"
"	mov	%r11, %rdi\n"		/* the .rodata name */
"	call	_elfsysv_resolve\n"
"	mov	%rax, %r11\n"		/* the faced export */
"	movdqu	0(%rsp), %xmm0\n"
"	movdqu	16(%rsp), %xmm1\n"
"	movdqu	32(%rsp), %xmm2\n"
"	movdqu	48(%rsp), %xmm3\n"
"	movdqu	64(%rsp), %xmm4\n"
"	movdqu	80(%rsp), %xmm5\n"
"	movdqu	96(%rsp), %xmm6\n"
"	movdqu	112(%rsp), %xmm7\n"
"	add	$128, %rsp\n"
"	pop	%rax\n"
"	pop	%r9\n"
"	pop	%r8\n"
"	pop	%rcx\n"
"	pop	%rdx\n"
"	pop	%rsi\n"
"	pop	%rdi\n"
"	jmp	*%r11\n"			/* tail-call the faced export, args intact */
"	.size	_elfsysv_thunk, .-_elfsysv_thunk\n"
"	.text\n"
);
