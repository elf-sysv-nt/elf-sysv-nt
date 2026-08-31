#!/usr/bin/env bash
# WP-27: certify the generic int-class face.
#
# Four properties. The generated unit reproduces byte for byte from its
# inputs; it instantiates exactly the int rows of sigclass.tsv, in face-table
# order, each bound to its face-table target; the committed unit assembles
# clean; and the macro itself moves arguments correctly across the seam --
# a Microsoft-ABI body called through the face from System V callers of
# arity 0, 4, 6, and 10 sees every argument exactly, returns through rax,
# and a sixteen-byte-aligned SSE store in the widest body proves the frame
# restored Microsoft alignment.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
face_dir=$here/..
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 1. reproduce
"$face_dir"/gen-int-faces.sh -o "$tmp/int-faces.gen.S"
if cmp -s "$tmp/int-faces.gen.S" "$face_dir/int-faces.gen.S"; then
  say "ok: int-faces.gen.S reproduces byte for byte"
else
  bad "int-faces.gen.S does not reproduce"
fi

# 2. exactly the int class, in order, bound to the face-table target
awk -F'\t' '$2 == "int" { print $1 }' "$face_dir/sigclass.tsv" > "$tmp/want.names"
sed -n 's/^\tsv2ms_int_face\t__face_\([^,]*\), .*/\1/p' "$face_dir/int-faces.gen.S" > "$tmp/have.names"
if cmp -s "$tmp/want.names" "$tmp/have.names"; then
  say "ok: exactly the int-class rows, in face-table order ($(wc -l < "$tmp/want.names") faces)"
else
  bad "emitted faces differ from the int class"
  diff "$tmp/want.names" "$tmp/have.names" | head -10
fi
awk -F'\t' 'NR==FNR { cls[$1]=$2; next } $2=="sv2ms" && cls[$1]=="int" { print $4 }' \
  "$face_dir/sigclass.tsv" "$face_dir/face.tsv" > "$tmp/want.targets"
sed -n 's/^\tsv2ms_int_face\t__face_[^,]*, \(.*\)$/\1/p' "$face_dir/int-faces.gen.S" > "$tmp/have.targets"
if cmp -s "$tmp/want.targets" "$tmp/have.targets"; then
  say "ok: every face bound to its face-table target"
else
  bad "face targets differ from the face table"
fi

# 3. the committed unit assembles
if gcc -I "$face_dir" -c "$face_dir/int-faces.gen.S" -o "$tmp/int-faces.o" 2> "$tmp/as.err"; then
  say "ok: committed unit assembles clean"
else
  bad "committed unit does not assemble"
  head -5 "$tmp/as.err"
fi

# 4. the macro moves arguments correctly across the seam
cat > "$tmp/faces.S" <<'EOF'
#include "sv2ms-int.inc"
	sv2ms_int_face	__face_body0, test_body0
	sv2ms_int_face	__face_body4, test_body4
	sv2ms_int_face	__face_body6, test_body6
	sv2ms_int_face	__face_body10, test_body10
EOF
cat > "$tmp/harness.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Distinct sixty-four-bit patterns; high bits set so a truncated or
 * misrouted slot cannot alias a correct one. */
#define A(n) (0xA000000000000000ull + 0x111111111111ull * (n))

static int checks;
static void eq(uint64_t got, uint64_t want, const char *who)
{
	checks++;
	if (got != want) {
		printf("FAIL: %s: got %llx want %llx\n", who,
		       (unsigned long long)got, (unsigned long long)want);
		__builtin_exit(1);
	}
}

/* The Microsoft-ABI bodies, the convention the DLL's bodies really have
 * (the host compiler's default here). Each verifies every argument and
 * returns a fold of them through rax. */
uint64_t test_body0(void) { return 0xC0DEull; }
uint64_t test_body4(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)
{
	eq(a1, A(1), "b4 a1"); eq(a2, A(2), "b4 a2");
	eq(a3, A(3), "b4 a3"); eq(a4, A(4), "b4 a4");
	return a1 ^ a2 ^ a3 ^ a4;
}
uint64_t test_body6(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
		    uint64_t a5, uint64_t a6)
{
	eq(a1, A(1), "b6 a1"); eq(a2, A(2), "b6 a2");
	eq(a3, A(3), "b6 a3"); eq(a4, A(4), "b6 a4");
	eq(a5, A(5), "b6 a5"); eq(a6, A(6), "b6 a6");
	return a1 ^ a2 ^ a3 ^ a4 ^ a5 ^ a6;
}
uint64_t test_body10(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
		     uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8,
		     uint64_t a9, uint64_t a10)
{
	/* An aligned SSE store: faults unless the face called at Microsoft
	 * stack alignment. */
	__attribute__((aligned(16))) volatile char buf[32];
	typedef long long v2 __attribute__((vector_size(16)));
	*(volatile v2 *)buf = (v2){ (long long)a1, (long long)a10 };
	eq(a1, A(1), "b10 a1"); eq(a2, A(2), "b10 a2");
	eq(a3, A(3), "b10 a3"); eq(a4, A(4), "b10 a4");
	eq(a5, A(5), "b10 a5"); eq(a6, A(6), "b10 a6");
	eq(a7, A(7), "b10 a7"); eq(a8, A(8), "b10 a8");
	eq(a9, A(9), "b10 a9"); eq(a10, A(10), "b10 a10");
	return a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10;
}

/* The faces, seen from the System V side. */
extern uint64_t __face_body0(void) __attribute__((sysv_abi));
extern uint64_t __face_body4(uint64_t, uint64_t, uint64_t, uint64_t)
	__attribute__((sysv_abi));
extern uint64_t __face_body6(uint64_t, uint64_t, uint64_t, uint64_t,
			     uint64_t, uint64_t) __attribute__((sysv_abi));
extern uint64_t __face_body10(uint64_t, uint64_t, uint64_t, uint64_t,
			      uint64_t, uint64_t, uint64_t, uint64_t,
			      uint64_t, uint64_t) __attribute__((sysv_abi));

/* A System V caller, as the ELF world will be. Its own frame sits where
 * the face's unconditional eight-slot copy overreads when arity is small,
 * so the zero- and four-argument calls also exercise the overread. */
__attribute__((sysv_abi)) static int run(void)
{
	eq(__face_body0(), 0xC0DEull, "b0 ret");
	eq(__face_body4(A(1), A(2), A(3), A(4)),
	   A(1) ^ A(2) ^ A(3) ^ A(4), "b4 ret");
	eq(__face_body6(A(1), A(2), A(3), A(4), A(5), A(6)),
	   A(1) ^ A(2) ^ A(3) ^ A(4) ^ A(5) ^ A(6), "b6 ret");
	eq(__face_body10(A(1), A(2), A(3), A(4), A(5), A(6), A(7), A(8),
			 A(9), A(10)),
	   A(1) + A(2) + A(3) + A(4) + A(5) + A(6) + A(7) + A(8) + A(9) + A(10),
	   "b10 ret");
	return 0;
}

int main(void)
{
	run();
	printf("ok: %d argument and return checks across the seam\n", checks);
	return 0;
}
EOF
if gcc -O2 -I "$face_dir" "$tmp/faces.S" "$tmp/harness.c" -o "$tmp/int-face-test" 2> "$tmp/cc.err"; then
  if "$tmp/int-face-test"; then
    say "ok: generic face carries arity 0, 4, 6, and 10 exactly"
  else
    bad "face harness reported a wrong argument or return"
  fi
else
  bad "face harness does not build"
  head -5 "$tmp/cc.err"
fi

[ $fail = 0 ] && say "PASS: int-face" || say "FAIL: int-face"
exit $fail
