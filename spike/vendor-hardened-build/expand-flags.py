#!/usr/bin/env python3
"""Expand el8's build flags from redhat-rpm-config, and diff ours against them.

The claim this settles is in toolchain/rpm/README.md: %optflags there was
written from RHEL 8's documented flags and never checked against a vendor
redhat-rpm-config. Documentation and the package disagree often enough that
"written from the documentation" is a recollection, so this reads the macro
file, the rpmrc and the two specs files and expands the chain itself.

Two expansions, both mechanical:

  vendor  rpmrc's `optflags: x86_64` and the macro file's %build_ldflags,
          resolved through %__global_compiler_flags, %_hardened_cflags,
          %_annotated_cflags and %_hardening_ldflags, then with each -specs=
          argument replaced by the flag that specs file injects.
  ours    toolchain/rpm/macros.elfsysvnt's %build_cflags and %build_ldflags,
          resolved the same way.

Usage:
  expand-flags.py --vendor-root DIR --ours FILE

  --vendor-root  an unpacked redhat-rpm-config, the directory holding
                 usr/lib/rpm/redhat/{macros,rpmrc,redhat-hardened-*}
  --ours         this project's macro file

Exit: 0 always when both inputs parse; the diff is the output, not the status.
"""
import argparse
import os
import re
import sys

DEF = re.compile(r'^%([A-Za-z_][A-Za-z0-9_]*)[ \t]+(.*)$')


def read_macros(path):
    r"""rpm macro definitions, with backslash continuations folded in.

    rpm's own continuation is a trailing backslash; our file writes `\\\` so
    that the shell, make and rpm each see what they need. Strip whatever run
    of them appears rather than counting.
    """
    defs = {}
    pending_name = None
    pending = []
    for raw in open(path, encoding='utf-8', errors='replace'):
        line = raw.rstrip('\n')
        cont = line.rstrip().endswith('\\')
        body = re.sub(r'\\+$', '', line.rstrip()).strip()
        if pending_name:
            pending.append(body)
            if not cont:
                defs[pending_name] = ' '.join(pending)
                pending_name, pending = None, []
            continue
        m = DEF.match(line)
        if not m:
            continue
        name, value = m.group(1), body[len(m.group(1)) + 1:].strip()
        if cont:
            pending_name, pending = name, [value]
        else:
            defs[name] = value
    if pending_name:
        defs[pending_name] = ' '.join(pending)
    return defs


def read_rpmrc_optflags(path, arch):
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'^optflags:\s+(\S+)\s+(.*)$', line)
        if m and m.group(1) == arch:
            return m.group(2).strip()
    return ''


def expand(text, defs, depth=0):
    """%{name}, %{?name}, %{?name:body}, %{!?name:body}, and bare %name.

    Deliberately small: this handles the forms the two files in question use
    and nothing else. A form it does not know is left verbatim, which shows up
    in the diff as an unexpanded term rather than silently vanishing.
    """
    if depth > 12:
        return text
    out = []
    i = 0
    while i < len(text):
        if text[i] != '%':
            out.append(text[i]); i += 1; continue
        if text.startswith('%{', i):
            j, level = i + 2, 1
            while j < len(text) and level:
                if text[j] == '{':
                    level += 1
                elif text[j] == '}':
                    level -= 1
                j += 1
            inner = text[i + 2:j - 1]
            out.append(expand(resolve(inner, defs), defs, depth + 1))
            i = j
            continue
        m = re.match(r'%([A-Za-z_][A-Za-z0-9_]*)', text[i:])
        if m:
            out.append(expand(defs.get(m.group(1), m.group(0)), defs, depth + 1))
            i += m.end()
            continue
        out.append(text[i]); i += 1
    return ''.join(out)


def resolve(inner, defs):
    if inner == 'nil':
        return ''
    neg = inner.startswith('!?')
    if neg:
        inner = inner[2:]
    elif inner.startswith('?'):
        inner = inner[1:]
    else:
        return defs.get(inner, '%{' + inner + '}')
    name, _, body = inner.partition(':')
    present = name in defs
    if neg:
        return body if not present else ''
    if body:
        return body if present else ''
    return defs.get(name, '')


def specs_injects(path):
    """What a gcc specs file adds to a command line.

    Every one of these wraps its payload in %{!flag:...} guards, so the
    payload is the last flag-shaped token in the file. Crude, and right for
    the three files el8 ships; a specs file it misreads shows up as a term
    that does not match, not as a silent drop.
    """
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        return None
    toks = re.findall(r'-[A-Za-z][A-Za-z0-9=_.-]*', text)
    return toks[-1] if toks else None


def terms(flagline, vendor_root, seen=None):
    """Split a flag line into comparable terms, resolving -specs= as we go.

    A -specs= argument is replaced by the flag that specs file injects, not
    kept as itself. The point of the comparison is what reaches the compiler,
    and writing out a flag Red Hat spells as a specs file is the whole shape
    of this project's substitution: keeping the two spellings apart would
    report a difference where there is none. Which files were resolved is
    collected in `seen` so the transcript can still name them.
    """
    out = []
    for t in flagline.split():
        if t.startswith('-specs='):
            path = t[len('-specs='):]
            local = os.path.join(vendor_root, path.lstrip('/'))
            injected = specs_injects(local)
            if seen is not None:
                seen.append('%s=%s' % (os.path.basename(path),
                                       injected if injected else 'unread'))
            out.append(injected if injected else t)
        elif t.startswith('--sysroot='):
            out.append('--sysroot')
        else:
            out.append(t)
    return out


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument('--vendor-root', required=True)
    ap.add_argument('--ours', required=True)
    ap.add_argument('--arch', default='x86_64')
    a = ap.parse_args()

    rdir = os.path.join(a.vendor_root, 'usr/lib/rpm/redhat')
    vdefs = read_macros(os.path.join(rdir, 'macros'))
    vdefs['optflags'] = read_rpmrc_optflags(os.path.join(rdir, 'rpmrc'), a.arch)

    seen = []
    v_c = terms(expand('%{build_cflags}', vdefs), a.vendor_root, seen)
    v_l = terms(expand('%{build_ldflags}', vdefs), a.vendor_root, seen)

    odefs = read_macros(a.ours)
    o_c = terms(expand('%{build_cflags}', odefs), a.vendor_root)
    o_l = terms(expand('%{build_ldflags}', odefs), a.vendor_root)

    print('specs_resolved=%s' % (' '.join(seen) or 'none'))

    def diff(label, vendor, ours):
        vs, os_ = set(vendor), set(ours)
        print('%s_vendor_terms=%d' % (label, len(vendor)))
        print('%s_ours_terms=%d' % (label, len(ours)))
        print('%s_absent_from_ours=%s' % (label, ' '.join(sorted(vs - os_)) or 'none'))
        print('%s_extra_in_ours=%s' % (label, ' '.join(sorted(os_ - vs)) or 'none'))

    diff('cflags', v_c, o_c)
    diff('ldflags', v_l, o_l)
    return 0


if __name__ == '__main__':
    sys.exit(main())
