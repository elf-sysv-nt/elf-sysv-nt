#!/usr/bin/env bash
# WP-27: certify the context-transparent face.
#
# Five properties. The generated unit reproduces byte for byte; it
# instantiates exactly ctx.tsv's rows, in order, bound to their targets;
# the committed unit assembles clean; the face is genuinely frameless (no
# call, no stack adjustment -- the property the whole shape exists for);
# and the semantic point holds at run time: through the ctx face, a
# longjmp taken after the setjmp call's stack region has been reused still
# returns to the true call site with the right value, while the same
# bodies behind the call-style int face -- the leaky control -- fail to.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
face_dir=$here/..
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 1. reproduce
"$face_dir"/gen-ctx-faces.sh -o "$tmp/ctx-faces.gen.S"
if cmp -s "$tmp/ctx-faces.gen.S" "$face_dir/ctx-faces.gen.S"; then
  say "ok: ctx-faces.gen.S reproduces byte for byte"
else
  bad "ctx-faces.gen.S does not reproduce"
fi

# 2. exactly ctx.tsv's rows, in order, bound to their targets
paste -d' ' <(cut -f1 "$face_dir/ctx.tsv" | sed 's/^/__face_/') \
            <(cut -f2 "$face_dir/ctx.tsv") > "$tmp/want.rows"
sed -n 's/^\tsv2ms_ctx_face\t\([^,]*\), \(.*\)$/\1 \2/p' \
  "$face_dir/ctx-faces.gen.S" > "$tmp/have.rows"
if cmp -s "$tmp/want.rows" "$tmp/have.rows"; then
  say "ok: exactly the ctx rows, in order ($(wc -l < "$tmp/want.rows") faces)"
else
  bad "emitted ctx faces differ from ctx.tsv"
  diff "$tmp/want.rows" "$tmp/have.rows" | head -10
fi

# every ctx name must be an int-class sv2ms row the int generator skipped
while IFS=$'\t' read -r name target; do
  grep -q "^$name	sv2ms	" "$face_dir/face.tsv" \
    || bad "$name is not an sv2ms row of the face table"
  grep -q "__face_$name," "$face_dir/int-faces.gen.S" \
    && bad "$name still has an int face; the two shapes would collide"
done < "$face_dir/ctx.tsv"
say "ok: ctx rows are sv2ms rows and carry no int face"

# 3. the committed unit assembles
if gcc -I "$face_dir" -c "$face_dir/ctx-faces.gen.S" -o "$tmp/ctx-faces.o" \
     2> "$tmp/as.err"; then
  say "ok: committed unit assembles clean"
else
  bad "committed unit does not assemble"
  head -5 "$tmp/as.err"
fi

# 4. frameless: the disassembled face is moves and one jump, nothing that
# makes or takes a frame
if objdump -d "$tmp/ctx-faces.o" | grep -E '^\s+[0-9a-f]+:' \
     | grep -Eq 'call|push|sub.*rsp|ret'; then
  bad "the ctx face is not frameless"
else
  say "ok: the ctx face makes no frame, no call, no return of its own"
fi

# 5. the semantic property, with a leaky control
cat > "$tmp/bodies.S" <<'EOF'
/* Microsoft-convention setjmp/longjmp stand-ins with the gendef shape:
 * store %rsp as-is and the return address at (%rsp); restore and resume
 * by returning on the captured stack.  No cygtls, which is the only
 * omission -- the capture and the resume are the properties under test. */
	.text
	.globl	ms_setjmp
	.seh_proc ms_setjmp
ms_setjmp:
	.seh_endprologue
	movq	%rbx, 0x8(%rcx)
	movq	%rsp, 0x10(%rcx)
	movq	%rbp, 0x18(%rcx)
	movq	%rsi, 0x20(%rcx)
	movq	%rdi, 0x28(%rcx)
	movq	%r12, 0x30(%rcx)
	movq	%r13, 0x38(%rcx)
	movq	%r14, 0x40(%rcx)
	movq	%r15, 0x48(%rcx)
	movq	(%rsp), %r10
	movq	%r10, 0x50(%rcx)
	xorl	%eax, %eax
	ret
	.seh_endproc
	.globl	ms_longjmp
	.seh_proc ms_longjmp
ms_longjmp:
	.seh_endprologue
	movl	%edx, %eax
	movq	0x8(%rcx), %rbx
	movq	0x18(%rcx), %rbp
	movq	0x20(%rcx), %rsi
	movq	0x28(%rcx), %rdi
	movq	0x30(%rcx), %r12
	movq	0x38(%rcx), %r13
	movq	0x40(%rcx), %r14
	movq	0x48(%rcx), %r15
	movq	0x10(%rcx), %rsp
	movq	0x50(%rcx), %r10
	addq	$8, %rsp
	jmp	*%r10
	.seh_endproc
EOF
cat > "$tmp/faces.S" <<'EOF'
#include "sv2ms-ctx.inc"
#include "sv2ms-int.inc"
	sv2ms_ctx_face	__ctx_setjmp, ms_setjmp
	sv2ms_ctx_face	__ctx_longjmp, ms_longjmp
	sv2ms_int_face	__call_setjmp, ms_setjmp
	sv2ms_int_face	__call_longjmp, ms_longjmp
EOF
cat > "$tmp/harness.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef int  (__attribute__((sysv_abi)) *setjmp_fn)(uint64_t *);
typedef void (__attribute__((sysv_abi)) *longjmp_fn)(uint64_t *, int);
extern int  __attribute__((sysv_abi)) __ctx_setjmp(uint64_t *);
extern void __attribute__((sysv_abi)) __ctx_longjmp(uint64_t *, int);
extern int  __attribute__((sysv_abi)) __call_setjmp(uint64_t *);
extern void __attribute__((sysv_abi)) __call_longjmp(uint64_t *, int);

static uint64_t buf[16] __attribute__((aligned(16)));

/* Calls at the same stack depth as the setjmp call site, so any face
 * frame captured there is reused and its return slot overwritten. */
static uint64_t __attribute__((sysv_abi, noinline)) churn(uint64_t x)
{
	return x * 2654435761u + 1;
}

static void __attribute__((sysv_abi, noinline)) fire(longjmp_fn lj)
{
	lj(buf, 42);
	fprintf(stderr, "FAIL: longjmp through the face returned\n");
	exit(1);
}

static int __attribute__((sysv_abi, noinline)) driver(setjmp_fn sj,
						      longjmp_fn lj)
{
	volatile uint64_t sentinel = 0x5157ABB1E5ULL;
	volatile uint64_t sum = 0;
	int r = sj(buf);
	if (r == 0) {
		sum += churn(1);
		sum += churn(sum);
		fire(lj);
		return 2;	/* fire does not return */
	}
	if (r != 42) {
		fprintf(stderr, "FAIL: longjmp value %d, not 42\n", r);
		return 1;
	}
	if (sentinel != 0x5157ABB1E5ULL) {
		fprintf(stderr, "FAIL: the frame was scribbled\n");
		return 1;
	}
	if (sum == 0) {
		fprintf(stderr, "FAIL: resumed before the churn\n");
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int ctl = argc > 1;
	int r = ctl ? driver(__call_setjmp, __call_longjmp)
		    : driver(__ctx_setjmp, __ctx_longjmp);
	if (r == 0)
		puts("resumed-at-the-true-call-site");
	return r;
}
EOF
gcc -std=gnu11 -O1 -g -Wall -mno-red-zone -I "$face_dir" \
    -o "$tmp/ctxrun.exe" "$tmp/harness.c" "$tmp/faces.S" "$tmp/bodies.S" \
  || bad "the semantic harness does not build"
if [ -x "$tmp/ctxrun.exe" ]; then
  if out=$("$tmp/ctxrun.exe") && [ "$out" = resumed-at-the-true-call-site ]; then
    say "ok: longjmp through the ctx face resumes at the true call site"
  else
    bad "the ctx face lost the resume: ${out:-no output}"
  fi
  # the leaky control: the same bodies behind the call-style face must not
  # survive the round trip -- any exit is acceptable except success
  if cout=$(timeout 10 "$tmp/ctxrun.exe" control 2>/dev/null) \
     && [ "$cout" = resumed-at-the-true-call-site ]; then
    bad "the call-style control resumed correctly; the check cannot fail"
  else
    say "ok: the call-style control loses the resume, so the check can fail"
  fi
fi

if [ "$fail" = 0 ]; then say "verdict: yes"; else say "verdict: no"; exit 1; fi
