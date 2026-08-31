#!/usr/bin/env bash
#
# WP-27: derive the signature classes of the sv2ms face surface.
#
# The sv2ms faces cannot all be generated from one generic shape. A face over
# a body whose every argument and result travels in the integer file needs no
# knowledge of the body's arity -- the same register move covers all of them
# -- but any float, double, or by-value aggregate anywhere in the signature
# changes which registers the two conventions use, and such a face must be
# generated from the true prototype. So the face generator needs to know, per
# export, which world the signature lives in, and this tool derives that from
# the one place the shape exists: the C prototype as the host compiler reads
# it.
#
# The method: a probe TU includes the host's own headers wholesale, gcc
# -aux-info emits every declaration it saw in canonical one-line form, and the
# rows whose names are sv2ms faces are classified:
#
#   int       every parameter and the result travel in the integer file
#             (integers, pointers, enums); coverable by a generic face
#   fp        a float, double, long double, or _Complex anywhere in the
#             signature; face must be generated from the recorded prototype
#   aggr      a by-value struct or union parameter or result; likewise
#   unlisted  no prototype in the probe's reach (Cygwin-internal exports,
#             underscore names); disposition decided by the face generator
#
# Output is sigclass.tsv: name, class, prototype ('-' when unlisted), in
# face.tsv's own order, one row per sv2ms face. Variadic and data rows are
# WP-24's and the linker's respectively and do not appear.
#
# Usage:
#   derive-sigclass.sh [-o FILE] [--face FILE] [--terse]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
face=$here/face.tsv
out=$here/sigclass.tsv
terse=0
while [ $# -gt 0 ]; do
  case $1 in
    -o) out=$2; shift 2;;
    --face) face=$2; shift 2;;
    --terse) terse=1; shift;;
    *) echo "unknown option: $1" >&2; exit 2;;
  esac
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# The probe TU. Feature macros first so the headers expose their full POSIX
# and GNU surface; then the breadth of the header set the exports live behind.
cat > "$tmp/probe.c" <<'EOF'
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <complex.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <setjmp.h>
#include <stdarg.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <glob.h>
#include <grp.h>
#include <pwd.h>
#include <iconv.h>
#include <langinfo.h>
#include <libgen.h>
#include <malloc.h>
#include <mntent.h>
#include <netdb.h>
#include <nl_types.h>
#include <poll.h>
#include <pthread.h>
#include <regex.h>
#include <sched.h>
#include <search.h>
#include <semaphore.h>
#include <spawn.h>
#include <termios.h>
#include <utime.h>
#include <utmp.h>
#include <utmpx.h>
#include <wordexp.h>
#include <aio.h>
#include <fenv.h>
#include <fnmatch.h>
#include <ftw.h>
#include <fts.h>
#include <inttypes.h>
#include <mqueue.h>
#include <monetary.h>
#include <ucontext.h>
#include <envz.h>
#include <argz.h>
#include <err.h>
#include <error.h>
#include <getopt.h>
#include <ifaddrs.h>
#include <memory.h>
#include <paths.h>
#include <resolv.h>
#include <stdio_ext.h>
#include <syslog.h>
#include <sys/acl.h>
#include <sys/cygwin.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/mount.h>
#include <sys/quota.h>
#include <sys/random.h>
#include <sys/reent.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/times.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
EOF

gcc -aux-info "$tmp/probe.aux" -fsyntax-only "$tmp/probe.c"

# aux-info rows -> "name<TAB>prototype", declarations only, deduplicated on
# first sight (the first declaration is the header's own; later ones repeat).
sed -n 's,^/\* [^*]*\*/ extern ,,p' "$tmp/probe.aux" \
| awk '
  {
    line = $0
    sub(/;[ \t]*$/, "", line)
    if (match(line, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/) == 0) next
    head = substr(line, 1, RSTART - 1) substr(line, RSTART)
    name = substr(line, RSTART)
    sub(/[ \t]*\(.*/, "", name)
    if (!(name in seen)) { seen[name] = 1; proto[name] = line }
  }
  END { for (n in proto) printf "%s\t%s\n", n, proto[n] }
' > "$tmp/protos.tsv"

# Classify each sv2ms row of the face table against the prototype map.
awk -F'\t' -v OFS='\t' -v protos="$tmp/protos.tsv" '
  BEGIN {
    while ((getline line < protos) > 0) {
      i = index(line, "\t")
      proto[substr(line, 1, i - 1)] = substr(line, i + 1)
    }
  }
  $2 != "sv2ms" { next }
  {
    name = $1
    if (!(name in proto)) { print name, "unlisted", "-"; n["unlisted"]++; next }
    p = proto[name]
    cls = "int"
    if (p ~ /(^|[^A-Za-z0-9_])(float|double|_Complex|_Float[0-9]+)([^A-Za-z0-9_]|$)/)
      cls = "fp"
    else {
      # by-value aggregates: a struct/union/typedef-of-struct token that is
      # not carried through a pointer. Strip pointered types, then look for
      # struct/union or a known by-value typedef.
      q = p
      gsub(/(struct|union)[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*\*+/, "PTR ", q)
      gsub(/(div_t|ldiv_t|lldiv_t|imaxdiv_t|fenv_t|fexcept_t)[ \t]*\*+/, "PTR ", q)
      if (q ~ /(^|[^A-Za-z0-9_])(struct|union)([^A-Za-z0-9_]|$)/ ||
          q ~ /(^|[^A-Za-z0-9_])(div_t|ldiv_t|lldiv_t|imaxdiv_t|fenv_t)([^A-Za-z0-9_]|$)/)
        cls = "aggr"
    }
    print name, cls, p
    n[cls]++
  }
  END {
    printf "int\t%d\nfp\t%d\naggr\t%d\nunlisted\t%d\n",
      n["int"], n["fp"], n["aggr"], n["unlisted"] > "/dev/stderr"
  }
' "$face" > "$tmp/sigclass.tsv" 2> "$tmp/counts"

if [ $terse = 1 ]; then cat "$tmp/counts"; exit 0; fi
cp "$tmp/sigclass.tsv" "$out"
cat "$tmp/counts" >&2
