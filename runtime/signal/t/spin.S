/*
 * spin.S -- a leaf whose only live variable is in the red zone.
 *
 * This is the thing the psABI promises and the thing a delivery can break. The
 * loop keeps its accumulator at -8(%rsp) and nowhere else, folds it once per
 * round, and counts the rounds in a register. A delivery that builds a frame
 * at the interrupted stack pointer writes over that word; a delivery that
 * skips the reserved 128 bytes first does not. Either way the caller can tell,
 * because the fold is reproducible from the seed and the round count.
 *
 * No call, no push, no frame. That is the point: -mno-red-zone would make the
 * compiler stop relying on those bytes, and this is written by hand precisely
 * because hand-written assembly is the residue the flag never reaches.
 */

#ifdef __ELF__
# define PROC(n)	.globl n; .type n,@function; n:
# define ENDPROC(n)	.size n, .-n
# define PROLOGUE_END
#else
# define PROC(n)	.globl n; n: ; .seh_proc n
# define ENDPROC(n)	.seh_endproc
# define PROLOGUE_END	.seh_endprologue
#endif

#define SEED	0x0123456789abcdef
#define PRIME	0x100000001b3

	.text

/* void sig_redzone_spin(volatile int *stop, uint64_t *out);
 * out[0] takes the folded accumulator, out[1] the number of rounds. */
PROC(sig_redzone_spin)
	PROLOGUE_END
	movabsq	$SEED, %rax
	movq	%rax, -8(%rsp)
	xorq	%r10, %r10
	movabsq	$PRIME, %r9
1:	movq	-8(%rsp), %rax
	imulq	%r9, %rax
	xorq	$0x9e37, %rax
	movq	%rax, -8(%rsp)
	incq	%r10
	movl	(%rdi), %ecx
	testl	%ecx, %ecx
	je	1b
	movq	-8(%rsp), %rax
	movq	%rax, (%rsi)
	movq	%r10, 8(%rsi)
	ret
/* A bound the harness reads to tell "back in the loop" from "still in a
 * delivery": a global label at the end of the leaf, no instruction of its
 * own, so the specimen it measures is byte-for-byte unchanged. */
	.globl sig_redzone_spin_end
sig_redzone_spin_end:
ENDPROC(sig_redzone_spin)

#ifdef __ELF__
	.section .note.GNU-stack,"",@progbits
#endif
