#!/usr/bin/env bash
# WP-27: certify the typed fp- and aggr-class faces.
#
# Four properties. The generated unit reproduces byte for byte from its
# inputs; it defines exactly the fp and aggr rows of sigclass.tsv, in
# order, each body aliased to its face-table target; the committed unit
# compiles clean against the host headers; and the emission itself moves
# arguments correctly across the seam -- the generator run over fabricated
# tables whose Microsoft-ABI bodies verify every argument, covering the
# shapes the real surface has: an all-double signature past both register
# files, mixed integer and floating arguments, floats, long doubles,
# complex values in and out, and by-value structs and unions in every
# passing class (packed in a register, returned through the hidden
# pointer, passed by memory).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
face_dir=$here/..
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 1. reproduce
"$face_dir"/gen-typed-faces.sh -o "$tmp/typed-faces.gen.c"
if cmp -s "$tmp/typed-faces.gen.c" "$face_dir/typed-faces.gen.c"; then
  say "ok: typed-faces.gen.c reproduces byte for byte"
else
  bad "typed-faces.gen.c does not reproduce"
fi

# 2. exactly the fp and aggr classes, in order, bodies aliased to targets
awk -F'\t' '$2 == "fp" || $2 == "aggr" { print $1 }' "$face_dir/sigclass.tsv" > "$tmp/want.names"
sed -n 's/^__attribute__((sysv_abi)) .*__face_\([A-Za-z0-9_]*\) (.*/\1/p' \
  "$face_dir/typed-faces.gen.c" > "$tmp/have.names"
if cmp -s "$tmp/want.names" "$tmp/have.names"; then
  say "ok: exactly the fp and aggr rows, in order ($(wc -l < "$tmp/want.names") faces)"
else
  bad "emitted faces differ from the fp and aggr classes"
  diff "$tmp/want.names" "$tmp/have.names" | head -10
fi
awk -F'\t' 'NR==FNR { cls[$1]=$2; next }
  $2=="sv2ms" && (cls[$1]=="fp" || cls[$1]=="aggr") { print $4 }' \
  "$face_dir/sigclass.tsv" "$face_dir/face.tsv" > "$tmp/want.targets"
sed -n 's/.*__asm__("\([^"]*\)");$/\1/p' "$face_dir/typed-faces.gen.c" > "$tmp/have.targets"
if cmp -s "$tmp/want.targets" "$tmp/have.targets"; then
  say "ok: every body aliased to its face-table target"
else
  bad "body aliases differ from the face table"
fi

# 3. the committed unit compiles clean against the host headers
if gcc -Wall -Werror -c "$face_dir/typed-faces.gen.c" -o "$tmp/typed-faces.o" 2> "$tmp/cc.err"; then
  say "ok: committed unit compiles clean"
else
  bad "committed unit does not compile"
  head -5 "$tmp/cc.err"
fi

# 4. the emission moves arguments across the seam. Fabricated tables, the
# generator's own emission path (--prelude swaps in the test's types), and
# Microsoft-ABI bodies that verify what arrived.
cat > "$tmp/face.tsv" <<'EOF'
tb_d8	sv2ms	-	tb_d8_ms
tb_mix	sv2ms	-	tb_mix_ms
tb_f	sv2ms	-	tb_f_ms
tb_ld	sv2ms	-	tb_ld_ms
tb_cx	sv2ms	-	tb_cx_ms
tb_sdiv	sv2ms	-	tb_sdiv_ms
tb_sbig	sv2ms	-	tb_sbig_ms
tb_sarg	sv2ms	-	tb_sarg_ms
tb_u	sv2ms	-	tb_u_ms
EOF
cat > "$tmp/sigclass.tsv" <<'EOF'
tb_d8	fp	double tb_d8 (double, double, double, double, double, double, double, double)
tb_mix	fp	long long int tb_mix (int, double, long int, float, void *, double)
tb_f	fp	float tb_f (float, float)
tb_ld	fp	long double tb_ld (long double, long double)
tb_cx	fp	complex double tb_cx (complex double, complex double)
tb_sdiv	aggr	struct tpair tb_sdiv (int, int)
tb_sbig	aggr	struct tquad tb_sbig (long int, long int, long int)
tb_sarg	aggr	long int tb_sarg (struct tquad, long int)
tb_u	aggr	long int tb_u (union tu, int)
EOF
cat > "$tmp/tb-types.h" <<'EOF'
#include <complex.h>
struct tpair { int q; int r; };
struct tquad { long a; long b; long c; long d; };
union tu { long l; double d; };
EOF
"$face_dir"/gen-typed-faces.sh --face "$tmp/face.tsv" --sigclass "$tmp/sigclass.tsv" \
  --prelude tb-types.h -o "$tmp/tb-faces.gen.c"
cat > "$tmp/harness.c" <<'EOF'
#include <stdio.h>
#include <complex.h>
struct tpair { int q; int r; };
struct tquad { long a; long b; long c; long d; };
union tu { long l; double d; };

static int checks;
static void eq(double got, double want, const char *who)
{
	checks++;
	if (got != want) {
		printf("FAIL: %s: got %g want %g\n", who, got, want);
		__builtin_exit(1);
	}
}

/* The Microsoft-ABI bodies (the host compiler's default): each verifies
 * every argument arrived exactly and returns a fold of them. */
double tb_d8_ms(double a1, double a2, double a3, double a4,
		double a5, double a6, double a7, double a8)
{
	eq(a1, 1.5, "d8 a1"); eq(a2, 2.5, "d8 a2"); eq(a3, 3.5, "d8 a3");
	eq(a4, 4.5, "d8 a4"); eq(a5, 5.5, "d8 a5"); eq(a6, 6.5, "d8 a6");
	eq(a7, 7.5, "d8 a7"); eq(a8, 8.5, "d8 a8");
	return a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8;
}
long long tb_mix_ms(int a1, double a2, long a3, float a4, void *a5, double a6)
{
	eq(a1, 11, "mix a1"); eq(a2, 2.25, "mix a2"); eq(a3, 33, "mix a3");
	eq(a4, 4.75f, "mix a4"); eq((long)a5, 0x5150, "mix a5");
	eq(a6, 6.125, "mix a6");
	return a1 + a3 + (long)a5;
}
float tb_f_ms(float a1, float a2)
{
	eq(a1, 1.25f, "f a1"); eq(a2, 2.75f, "f a2");
	return a1 * a2;
}
long double tb_ld_ms(long double a1, long double a2)
{
	eq((double)a1, 3.5, "ld a1"); eq((double)a2, 0.5, "ld a2");
	return a1 / a2;
}
double complex tb_cx_ms(double complex a1, double complex a2)
{
	eq(creal(a1), 1.0, "cx re1"); eq(cimag(a1), 2.0, "cx im1");
	eq(creal(a2), 3.0, "cx re2"); eq(cimag(a2), 4.0, "cx im2");
	return a1 * a2;
}
struct tpair tb_sdiv_ms(int a1, int a2)
{
	eq(a1, 17, "sdiv a1"); eq(a2, 5, "sdiv a2");
	return (struct tpair){ a1 / a2, a1 % a2 };
}
struct tquad tb_sbig_ms(long a1, long a2, long a3)
{
	eq(a1, 100, "sbig a1"); eq(a2, 200, "sbig a2"); eq(a3, 300, "sbig a3");
	return (struct tquad){ a1, a2, a3, a1 + a2 + a3 };
}
long tb_sarg_ms(struct tquad a1, long a2)
{
	eq(a1.a, 100, "sarg .a"); eq(a1.b, 200, "sarg .b");
	eq(a1.c, 300, "sarg .c"); eq(a1.d, 600, "sarg .d");
	eq(a2, 7, "sarg a2");
	return a1.d + a2;
}
long tb_u_ms(union tu a1, int a2)
{
	eq(a1.l, 0x4242, "u a1"); eq(a2, 9, "u a2");
	return a1.l + a2;
}

/* The faces, seen from the System V side. */
#define SV __attribute__((sysv_abi))
extern SV double __face_tb_d8(double, double, double, double,
			      double, double, double, double);
extern SV long long __face_tb_mix(int, double, long, float, void *, double);
extern SV float __face_tb_f(float, float);
extern SV long double __face_tb_ld(long double, long double);
extern SV double complex __face_tb_cx(double complex, double complex);
extern SV struct tpair __face_tb_sdiv(int, int);
extern SV struct tquad __face_tb_sbig(long, long, long);
extern SV long __face_tb_sarg(struct tquad, long);
extern SV long __face_tb_u(union tu, int);

/* A System V caller, as the ELF world will be. */
SV static int run(void)
{
	eq(__face_tb_d8(1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5), 40.0, "d8 ret");
	eq(__face_tb_mix(11, 2.25, 33, 4.75f, (void *)0x5150, 6.125),
	   11 + 33 + 0x5150, "mix ret");
	eq(__face_tb_f(1.25f, 2.75f), 1.25f * 2.75f, "f ret");
	eq((double)__face_tb_ld(3.5L, 0.5L), 7.0, "ld ret");
	double complex c = __face_tb_cx(1.0 + 2.0 * I, 3.0 + 4.0 * I);
	eq(creal(c), -5.0, "cx re ret"); eq(cimag(c), 10.0, "cx im ret");
	struct tpair p = __face_tb_sdiv(17, 5);
	eq(p.q, 3, "sdiv .q"); eq(p.r, 2, "sdiv .r");
	struct tquad q = __face_tb_sbig(100, 200, 300);
	eq(q.a, 100, "sbig .a"); eq(q.b, 200, "sbig .b");
	eq(q.c, 300, "sbig .c"); eq(q.d, 600, "sbig .d");
	eq(__face_tb_sarg(q, 7), 607, "sarg ret");
	union tu u = { .l = 0x4242 };
	eq(__face_tb_u(u, 9), 0x4242 + 9, "u ret");
	return 0;
}

int main(void)
{
	run();
	printf("ok: %d argument and return checks across the seam\n", checks);
	return 0;
}
EOF
if gcc -O2 -I "$tmp" "$tmp/tb-faces.gen.c" "$tmp/harness.c" -o "$tmp/typed-face-test" 2> "$tmp/h.err"; then
  if "$tmp/typed-face-test"; then
    say "ok: typed emission carries fp and aggregate shapes exactly"
  else
    bad "typed harness reported a wrong argument or return"
  fi
else
  bad "typed harness does not build"
  head -5 "$tmp/h.err"
fi

[ $fail = 0 ] && say "PASS: typed-face" || say "FAIL: typed-face"
exit $fail
