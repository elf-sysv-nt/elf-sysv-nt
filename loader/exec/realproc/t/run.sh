#!/usr/bin/env bash
# WP-56 reent-tls-bringup, item 1: certify the real-process stub compatibility
# layer (loader/exec/realproc). Three stages, each a done-when the layer owes:
#
#   unit    the freestanding string/parse primitives (realproc-str.c) are pure
#           over their inputs; built natively with -DELFSYSV_REALPROC and held
#           to known results and to the platform libc they stand in for. Always
#           runs -- no build product needed.
#   file    rp_slurp (realproc-file.c) reads an image with Win32 -- no libc
#           call, so no crossing -- and is held natively to files written on
#           the spot, given a Windows-form path. Always runs.
#   plain   including realproc.h WITHOUT ELFSYSV_REALPROC is the identity seam:
#           the RP_* macros are the plain libc, so a translation unit that
#           includes it is byte-for-byte the program it was. A compile check
#           that the plain-PE path the WP-41 exec-* certifications drive is
#           untouched. Always runs.
#   cross   version-cross.c -- stub.c's --version path built from the shipped
#           units in the real-process shape (-nostdlib, WP-26 crt0.o, -lcygwin,
#           -lgcc, -DELFSYSV_REALPROC) -- reaches main across realproc-cross.c's
#           cygwin_internal bridge and emits the RELEASE line across its
#           sysv_abi puts thunk, control surviving. SKIPs (verdict yes) when the
#           faced elfsysv1.dll or the WP-26 build tree are absent, both being
#           uncommitted build products.
#   ecross  stderr-cross.c -- the stub's stderr diagnostics reach fd 2
#           across the faced runtime through RP_EPUTS's sysv_abi write(2)
#           thunk, the route spike/reent-stub-stderr-crossing measured. Same
#           build gate as cross.
#
# The faced runtime wedges on a host pty, so the cross probe is run detached via
# cmd with stdin from NUL, as the reent-stub-* spikes do.
set -u

here=$(cd "$(dirname "$0")" && pwd)
dir=$(cd "$here/.." && pwd)
repo=$(cd "$dir/../../.." && pwd)
fail=0

say() { printf '%s\n' "$*"; }

# --- unit: freestanding primitives, native ------------------------------
u=$(mktemp -d "${TMPDIR:-/tmp}/rp-unit.XXXXXX")
if gcc -O2 -Wall -Wextra -Werror -DELFSYSV_REALPROC \
     "$here/unit.c" "$dir/realproc-str.c" "$dir/realproc-fmt.c" -o "$u/unit" 2>"$u/err"; then
if "$u/unit"; then say "unit=pass"; else say "unit=fail"; fail=1; fi
else
say "unit=fail (build)"; sed 's/^/    /' "$u/err"; fail=1
fi
rm -rf "$u"

# --- plain: the identity seam still builds ------------------------------
p=$(mktemp -d "${TMPDIR:-/tmp}/rp-plain.XXXXXX")
cat > "$p/plain.c" <<'CEOF'
#include "realproc.h"
int probe(const char *a, const char *b) { return RP_STRCMP(a, b); }
size_t probe_len(const char *s) { return RP_STRLEN(s); }
int probe_fmt(char *b, size_t n, unsigned long long v)
{ return RP_SNPRINTF(b, n, "0x%llx", v); }
CEOF
if gcc -O2 -Wall -Wextra -Werror -I"$dir" -c "$p/plain.c" -o "$p/plain.o" 2>"$p/err"; then
say "plain=pass"
else
say "plain=fail"; sed 's/^/    /' "$p/err"; fail=1
fi
rm -rf "$p"

# --- file: rp_slurp reads the image host-safe, native ------------------
f=$(mktemp -d "${TMPDIR:-/tmp}/rp-file.XXXXXX")
if gcc -O2 -Wall -Wextra -Werror -DELFSYSV_REALPROC -I"$dir" "$here/file-probe.c" "$dir/realproc-file.c" -o "$f/file" 2>"$f/err"; then
# rp_slurp opens with Win32 CreateFileA, so the probe is handed a Windows-form
# path (cygpath -w); the path form a real-process stub receives is the open
# question SLURP-REROUTE.md records, separate from the read being correct.
if "$f/file" "$(cygpath -w "$f")"; then say "file=pass"; else say "file=fail"; fail=1; fi
else
say "file=fail (build)"; sed 's/^/    /' "$f/err"; fail=1
fi
rm -rf "$f"

# --- cross: --version across the faced runtime --------------------------
main=$(cd "$(git -C "$repo" rev-parse --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd)
[ -n "$main" ] || main=$repo
out=$main/a/build/wp27-face
dll=$out/elfsysv1.dll
build=$main/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin

if [ ! -f "$dll" ] || [ ! -f "$build/crt0.o" ]; then
say "cross=skip (no faced DLL or WP-26 build tree)"
say "verdict=$([ $fail -eq 0 ] && echo yes || echo no)"
exit $fail
fi

t=$(mktemp -d "${TMPDIR:-/tmp}/rp-cross.XXXXXX")
trap 'rm -rf "$t"' EXIT
CF="-std=gnu11 -O1 -g -mno-red-zone -fno-stack-protector -nostdlib -DELFSYSV_REALPROC"
LIBS="$build/crt0.o -L$build -lcygwin -lkernel32 -lgcc"
if gcc $CF -I"$dir" -o "$t/vc.exe" \
     "$here/version-cross.c" "$dir/realproc-str.c" "$dir/realproc-cross.c" \
     $LIBS 2>"$t/err"; then
cp "$t/vc.exe" "$out/rp-vc.exe"
( cd "$out" && rm -f rp-vc.out \
  && timeout 40 cmd /c "rp-vc.exe --version > rp-vc.out 2>&1 < NUL" ) 2>/dev/null
o=$(tr -d '\r' < "$out/rp-vc.out" 2>/dev/null)
rm -f "$out/rp-vc.exe" "$out/rp-vc.out"
if printf '%s' "$o" | grep -q '^A:reached-main' \
   && printf '%s' "$o" | grep -q '^elfsysv-stub 1\.0$' \
   && printf '%s' "$o" | grep -q '^E:after-version'; then
say "cross=pass"
else
say "cross=fail"; printf '%s\n' "$o" | sed 's/^/    out: /'; fail=1
fi
else
say "cross=fail (build)"; sed 's/^/    /' "$t/err"; fail=1
fi

# --- ecross: stderr diagnostics across the faced runtime ----------------
if gcc $CF -I"$dir" -o "$t/ec.exe" \
     "$here/stderr-cross.c" "$dir/realproc-str.c" "$dir/realproc-cross.c" \
     $LIBS 2>"$t/eerr"; then
cp "$t/ec.exe" "$out/rp-ec.exe"
( cd "$out" && rm -f rp-ec.out \
  && timeout 40 cmd /c "rp-ec.exe > rp-ec.out 2>&1 < NUL" ) 2>/dev/null
eo=$(tr -d '\r' < "$out/rp-ec.out" 2>/dev/null)
rm -f "$out/rp-ec.exe" "$out/rp-ec.out"
if printf '%s' "$eo" | grep -q '^A:reached-main' \
   && printf '%s' "$eo" | grep -q '^D:crossed-stderr' \
   && printf '%s' "$eo" | grep -q '^E:after-eputs'; then
say "ecross=pass"
else
say "ecross=fail"; printf '%s\n' "$eo" | sed 's/^/    out: /'; fail=1
fi
else
say "ecross=fail (build)"; sed 's/^/    /' "$t/eerr"; fail=1
fi

say "verdict=$([ $fail -eq 0 ] && echo yes || echo no)"
exit $fail
