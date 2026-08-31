#!/usr/bin/env python3
"""WP-55: extract the translation tables from the two header trees.

The divergence classes DR-0000 names, read mechanically from headers on
both sides of the boundary rather than remembered: errno values, signal
numbers, flag constants, and the layouts of the structs that cross.

The Linux side is el8's vendored glibc header set (veneer/include/) over
the pinned el8 kernel headers (fetched, not vendored -- DR-0002).  The
Cygwin side is the WP-26 newlib-cygwin tree at b11613e47 (DR-0007).
Both sides are compiled by the native gcc; values come from the emitted
assembly and layouts from DWARF, so nothing here depends on running
target code.

Names are discovered from the Linux side's preprocessor output (-dM) by
family pattern; each discovered name is then probed on both sides.  A
name the Cygwin side does not define is recorded with '-', which is a
finding, not a failure.  A probe that does not fold to an integer
constant (e.g. glibc's SIGRTMIN, a function call) is dropped by the
error-pruning loop and recorded in dropped.tsv with the compiler's
reason, so the exclusion list maintains itself.

Output (all TSV, deterministically sorted):
  errno-map.tsv   name, linux, cygwin
  signal-map.tsv  name, linux, cygwin
  flags.tsv       family, name, linux, cygwin
  layouts.tsv     struct, member, linux_off, linux_size, cyg_off, cyg_size
  dropped.tsv     side, name, reason
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# -- the two sides ----------------------------------------------------------

def gcc_builtin_include():
    out = subprocess.run(["gcc", "-print-file-name=include"],
                         capture_output=True, text=True, check=True)
    return out.stdout.strip()

def linux_flags(veneer_include, kernel_include):
    return ["-nostdinc", "-U__CYGWIN__", "-U_WIN32", "-D__linux__=1",
            "-D__gnu_linux__=1", "-D_GNU_SOURCE",
            "-I", veneer_include, "-I", kernel_include,
            "-I", gcc_builtin_include()]

def cygwin_flags(nc_root):
    return ["-nostdinc", "-D_GNU_SOURCE",
            "-I", os.path.join(nc_root, "winsup", "cygwin", "include"),
            "-I", os.path.join(nc_root, "newlib", "libc", "include"),
            "-I", gcc_builtin_include()]

# -- umbrella headers -------------------------------------------------------

UMBRELLA = [
    "errno.h", "signal.h", "fcntl.h", "unistd.h", "dirent.h",
    "sys/stat.h", "sys/mman.h", "sys/socket.h", "sys/un.h",
    "netinet/in.h", "netinet/tcp.h", "poll.h", "sys/resource.h",
    "sys/wait.h", "sched.h", "time.h", "sys/time.h", "termios.h", "sys/uio.h",
    "sys/utsname.h", "sys/statvfs.h", "sys/ioctl.h", "stddef.h",
]

def umbrella_source(guarded):
    lines = []
    for h in UMBRELLA:
        if guarded:
            lines.append("#if defined(__has_include)")
            lines.append("#if __has_include(<%s>)" % h)
            lines.append("#include <%s>" % h)
            lines.append("#endif")
            lines.append("#endif")
        else:
            lines.append("#include <%s>" % h)
    return "\n".join(lines) + "\n"

# -- name discovery ---------------------------------------------------------

# Flag families, discovered over the full umbrella.  The five the plan
# names -- O_*, F_*, AT_*, MAP_*, SOCK_* -- and their relatives down the
# same headers.
FLAG_FAMILIES = [
    ("O",       r"O_[A-Z0-9_]+"),
    ("F",       r"F_[A-Z0-9_]+"),
    ("FD",      r"FD_[A-Z0-9_]+"),
    ("AT",      r"AT_[A-Z0-9_]+"),
    ("MAP",     r"MAP_[A-Z0-9_]+"),
    ("PROT",    r"PROT_[A-Z0-9_]+"),
    ("MADV",    r"MADV_[A-Z0-9_]+"),
    ("MCL",     r"MCL_[A-Z0-9_]+"),
    ("MS",      r"MS_[A-Z0-9_]+"),
    ("SOCK",    r"SOCK_[A-Z0-9_]+"),
    ("SO",      r"SO_[A-Z0-9_]+"),
    ("SOL",     r"SOL_[A-Z0-9_]+"),
    ("AF",      r"AF_[A-Z0-9_]+"),
    ("PF",      r"PF_[A-Z0-9_]+"),
    ("MSG",     r"MSG_[A-Z0-9_]+"),
    ("SHUT",    r"SHUT_[A-Z0-9_]+"),
    ("IPPROTO", r"IPPROTO_[A-Z0-9_]+"),
    ("POLL",    r"POLL[A-Z]+"),
    ("SEEK",    r"SEEK_[A-Z]+"),
    ("S_I",     r"S_I[A-Z]+"),
    ("RLIMIT",  r"RLIMIT_[A-Z]+"),
    ("RLIM",    r"RLIM_[A-Z_]+"),
    ("CLOCK",   r"CLOCK_[A-Z_]+"),
    ("WAIT",    r"W(NOHANG|UNTRACED|CONTINUED|EXITED|STOPPED|NOWAIT|ALL|CLONE)"),
]

def discover(names_from, pattern):
    rx = re.compile(r"^%s$" % pattern)
    return sorted(n for n in names_from if rx.match(n))

def dump_macros(flags, source):
    """Object-like macro names from gcc -E -dM over SOURCE."""
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, "u.c")
        with open(src, "w") as f:
            f.write(source)
        out = subprocess.run(["gcc", "-E", "-dM"] + flags + [src],
                             capture_output=True, text=True)
        if out.returncode != 0:
            sys.stderr.write(out.stderr)
            raise RuntimeError("umbrella -dM failed")
    names = set()
    for line in out.stdout.splitlines():
        m = re.match(r"#define (\w+) ", line)
        if m:  # object-like only: function-like has '(' right after name
            names.add(m.group(1))
    return names

# -- constant probing -------------------------------------------------------

def probe_values(flags, source_prefix, names, dropped, side):
    """Compile a probe per NAME, parse .quad values from the assembly.

    Names whose probe does not compile are pruned by re-reading gcc's
    error lines and retrying, so a non-constant macro removes itself and
    is recorded rather than breaking the run.
    """
    live = list(names)
    values = {}
    for _ in range(50):
        with tempfile.TemporaryDirectory() as td:
            src = os.path.join(td, "p.c")
            asm = os.path.join(td, "p.s")
            body = [source_prefix]
            for i, n in enumerate(live):
                body.append("#ifdef %s" % n)
                body.append("const unsigned long long xlat_%d = "
                            "(unsigned long long)(%s);" % (i, n))
                body.append("#endif")
            text = "\n".join(body) + "\n"
            with open(src, "w") as f:
                f.write(text)
            linemap = {}
            for lineno, line in enumerate(text.splitlines(), 1):
                m = re.match(r"const unsigned long long xlat_(\d+) ", line)
                if m:
                    linemap[lineno] = live[int(m.group(1))]
            r = subprocess.run(["gcc", "-S"] + flags + ["-o", asm, src],
                               capture_output=True, text=True)
            if r.returncode != 0:
                bad = set()
                for m in re.finditer(r"p\.c:(\d+):", r.stderr):
                    n = linemap.get(int(m.group(1)))
                    if n:
                        bad.add(n)
                if not bad:
                    sys.stderr.write(r.stderr)
                    raise RuntimeError("%s probe failed unattributably" % side)
                for n in sorted(bad):
                    dropped.append((side, n,
                                    "does not fold to an integer constant"))
                    live.remove(n)
                continue
            with open(asm) as f:
                asmtext = f.read()
            idx = {}
            for m in re.finditer(
                    r"^xlat_(\d+):\s*\n\s*\.quad\s+(-?\d+)", asmtext, re.M):
                idx[int(m.group(1))] = int(m.group(2)) & 0xFFFFFFFFFFFFFFFF
            for i, n in enumerate(live):
                if i in idx:
                    values[n] = idx[i]
            return values
    raise RuntimeError("probe pruning did not converge")

# -- layouts via DWARF ------------------------------------------------------

# The structs that cross the boundary.  Typedef'd types are declared by
# their typedef name; the rest as struct tags.
LAYOUT_TYPES = [
    ("struct stat", "stat"),
    ("struct dirent", "dirent"),
    ("struct termios", "termios"),
    ("struct sockaddr", "sockaddr"),
    ("struct sockaddr_in", "sockaddr_in"),
    ("struct sockaddr_in6", "sockaddr_in6"),
    ("struct sockaddr_un", "sockaddr_un"),
    ("struct sockaddr_storage", "sockaddr_storage"),
    ("struct rlimit", "rlimit"),
    ("struct rusage", "rusage"),
    ("struct sigaction", "sigaction"),
    ("struct timespec", "timespec"),
    ("struct timeval", "timeval"),
    ("struct itimerval", "itimerval"),
    ("struct pollfd", "pollfd"),
    ("struct iovec", "iovec"),
    ("struct msghdr", "msghdr"),
    ("struct cmsghdr", "cmsghdr"),
    ("struct flock", "flock"),
    ("struct linger", "linger"),
    ("struct utsname", "utsname"),
    ("struct statvfs", "statvfs"),
    ("sigset_t", "sigset_t"),
    ("stack_t", "stack_t"),
]

def layout_source(guarded, live):
    lines = [umbrella_source(guarded)]
    for ctype, tag in LAYOUT_TYPES:
        if tag in live:
            lines.append("%s xlat_v_%s;" % (ctype, tag))
    return "\n".join(lines) + "\n"

def probe_layouts(flags, guarded, dropped, side):
    live = [tag for _, tag in LAYOUT_TYPES]
    for _ in range(len(LAYOUT_TYPES) + 1):
        with tempfile.TemporaryDirectory() as td:
            src = os.path.join(td, "l.c")
            obj = os.path.join(td, "l.o")
            text = layout_source(guarded, live)
            with open(src, "w") as f:
                f.write(text)
            linemap = {}
            for lineno, line in enumerate(text.splitlines(), 1):
                m = re.search(r"xlat_v_(\w+);", line)
                if m:
                    linemap[lineno] = m.group(1)
            r = subprocess.run(["gcc", "-c", "-g", "-gdwarf-4"] + flags +
                               ["-o", obj, src],
                               capture_output=True, text=True)
            if r.returncode != 0:
                bad = set()
                for m in re.finditer(r"l\.c:(\d+):", r.stderr):
                    n = linemap.get(int(m.group(1)))
                    if n:
                        bad.add(n)
                if not bad:
                    sys.stderr.write(r.stderr)
                    raise RuntimeError("%s layout probe failed" % side)
                for n in sorted(bad):
                    dropped.append((side, "struct:" + n,
                                    "type not declared on this side"))
                    live.remove(n)
                continue
            d = subprocess.run(["objdump", "--dwarf=info", obj],
                               capture_output=True, text=True, check=True)
            return parse_dwarf(d.stdout, live)
    raise RuntimeError("layout pruning did not converge")

def parse_dwarf(text, live):
    """offset -> DIE dict, then walk xlat_v_* variables' struct types."""
    dies = {}
    order = []
    cur = None
    for line in text.splitlines():
        m = re.match(r" <(\d+)><([0-9a-f]+)>: Abbrev Number: \d+ \(DW_TAG_(\w+)\)",
                     line)
        if m:
            cur = {"depth": int(m.group(1)), "tag": m.group(3),
                   "attrs": {}, "children": []}
            off = int(m.group(2), 16)
            dies[off] = cur
            order.append(off)
            continue
        m = re.match(r"    <[0-9a-f]+>   (DW_AT_\w+)\s*:(.*)", line)
        if m and cur is not None:
            cur["attrs"][m.group(1)] = m.group(2).strip()
    stack = []
    for off in order:
        die = dies[off]
        while stack and dies[stack[-1]]["depth"] >= die["depth"]:
            stack.pop()
        if stack:
            dies[stack[-1]]["children"].append(off)
        stack.append(off)

    def attr(off, name):
        return dies[off]["attrs"].get(name)

    def name_of(off):
        v = attr(off, "DW_AT_name")
        if v is None:
            return None
        m = re.search(r"\): (.*)$", v)
        return m.group(1) if m else v

    def type_ref(off):
        v = attr(off, "DW_AT_type")
        if v is None:
            return None
        m = re.search(r"<0x([0-9a-f]+)>", v)
        return int(m.group(1), 16) if m else None

    def strip(off):
        seen = set()
        while off is not None and off not in seen:
            seen.add(off)
            if dies[off]["tag"] in ("typedef", "const_type", "volatile_type"):
                nxt = type_ref(off)
                if nxt is None:
                    break
                off = nxt
            else:
                break
        return off

    def size_of(off):
        off = strip(off)
        if off is None:
            return None
        v = attr(off, "DW_AT_byte_size")
        if v is not None and re.match(r"^\d+$", v):
            return int(v)
        if dies[off]["tag"] == "array_type":
            elem = size_of(type_ref(off))
            count = None
            for c in dies[off]["children"]:
                if dies[c]["tag"] == "subrange_type":
                    ub = attr(c, "DW_AT_upper_bound")
                    if ub is not None and re.match(r"^\d+$", ub):
                        count = int(ub) + 1
            if elem is not None and count is not None:
                return elem * count
        return None

    def members(off, base, prefix, out):
        for c in dies[off]["children"]:
            if dies[c]["tag"] != "member":
                continue
            loc = attr(c, "DW_AT_data_member_location")
            mo = base + (int(loc) if loc and re.match(r"^\d+$", loc) else 0)
            nm = name_of(c)
            t = strip(type_ref(c))
            if nm is None and t is not None and \
               dies[t]["tag"] in ("structure_type", "union_type"):
                members(t, mo, prefix, out)
                continue
            nm = prefix + (nm or "?")
            out.append((nm, mo, size_of(type_ref(c))))

    result = {}
    for off in order:
        die = dies[off]
        if die["tag"] != "variable":
            continue
        nm = name_of(off)
        if not nm or not nm.startswith("xlat_v_"):
            continue
        tag = nm[len("xlat_v_"):]
        if tag not in live:
            continue
        t = strip(type_ref(off))
        rows = []
        total = size_of(type_ref(off))
        if t is not None and dies[t]["tag"] in ("structure_type", "union_type"):
            members(t, 0, "", rows)
        result[tag] = {"size": total, "members": rows}
    return result

# -- table emission ---------------------------------------------------------

def fmt(v):
    return str(v) if v is not None else "-"

def write_tsv(path, header_lines, rows):
    with open(path, "w", newline="\n") as f:
        for h in header_lines:
            f.write("# " + h + "\n")
        for r in rows:
            f.write("\t".join(str(c) for c in r) + "\n")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nc", default="/c/-/repo/newlib-cygwin")
    ap.add_argument("--kernel-include", required=True)
    ap.add_argument("--veneer-include",
                    default=os.path.join(HERE, "..", "include"))
    ap.add_argument("--out", default=HERE)
    args = ap.parse_args()

    lflags = linux_flags(args.veneer_include, args.kernel_include)
    cflags = cygwin_flags(args.nc)
    dropped = []

    # errno and signal names from their own headers, so E* baud rates
    # and the like from termios never enter the errno table.
    lerr = dump_macros(lflags, "#include <errno.h>\n")
    lsig = dump_macros(lflags, "#include <signal.h>\n")
    lall = dump_macros(lflags, umbrella_source(False))

    errno_names = discover(lerr, r"E[A-Z][A-Z0-9]*")
    signal_names = discover(lsig, r"SIG[A-Z][A-Z0-9]*")
    flag_names = []
    claimed = set()
    for fam, pat in FLAG_FAMILIES:
        for n in discover(lall, pat):
            if n not in claimed:
                flag_names.append((fam, n))
                claimed.add(n)

    every = errno_names + signal_names + [n for _, n in flag_names]
    lvals = probe_values(lflags, umbrella_source(False), every,
                         dropped, "linux")
    cvals = probe_values(cflags, umbrella_source(True), every,
                         dropped, "cygwin")

    llay = probe_layouts(lflags, False, dropped, "linux")
    clay = probe_layouts(cflags, True, dropped, "cygwin")

    pin = ["WP-55 translation table -- generated by extract-tables.py;"
           " do not edit.",
           "linux side: veneer/include (glibc-headers-2.28-251.el8_10.40)"
           " over kernel-headers-4.18.0-553.el8_10 (fetch-kernel-headers.sh)",
           "cygwin side: newlib-cygwin at b11613e47 (DR-0007)"]

    def pair_rows(names):
        return [(n, fmt(lvals.get(n)), fmt(cvals.get(n)))
                for n in names if n in lvals]

    write_tsv(os.path.join(args.out, "errno-map.tsv"),
              pin + ["name\tlinux\tcygwin"], pair_rows(errno_names))
    write_tsv(os.path.join(args.out, "signal-map.tsv"),
              pin + ["name\tlinux\tcygwin"], pair_rows(signal_names))
    write_tsv(os.path.join(args.out, "flags.tsv"),
              pin + ["family\tname\tlinux\tcygwin"],
              [(fam, n, fmt(lvals.get(n)), fmt(cvals.get(n)))
               for fam, n in flag_names if n in lvals])

    lrows = []
    for _, tag in LAYOUT_TYPES:
        if tag not in llay:
            continue
        L = llay[tag]
        C = clay.get(tag)
        lrows.append((tag, "(sizeof)", fmt(L["size"]), "-",
                      fmt(C["size"]) if C else "-", "-"))
        cmem = {n: (o, s) for n, o, s in (C["members"] if C else [])}
        lnames = {n for n, _, _ in L["members"]}
        for n, o, s in L["members"]:
            co, cs = cmem.get(n, (None, None))
            lrows.append((tag, n, fmt(o), fmt(s), fmt(co), fmt(cs)))
        for n, o, s in (C["members"] if C else []):
            if n not in lnames:
                lrows.append((tag, n, "-", "-", fmt(o), fmt(s)))
    write_tsv(os.path.join(args.out, "layouts.tsv"),
              pin + ["struct\tmember\tlinux_off\tlinux_size"
                     "\tcyg_off\tcyg_size"], lrows)

    write_tsv(os.path.join(args.out, "dropped.tsv"),
              pin + ["side\tname\treason"], sorted(set(dropped)))

    sys.stderr.write("errno %d  signal %d  flags %d  layout rows %d"
                     "  dropped %d\n"
                     % (len(errno_names), len(signal_names),
                        len(flag_names), len(lrows), len(dropped)))

if __name__ == "__main__":
    main()
