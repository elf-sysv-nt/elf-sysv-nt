#!/usr/bin/env python3
#
# WP-T1's fixture corpus: a small set of ELF64 images, one well-formed and the
# rest deliberately ugly, each paired with the verdict the WP-31 parser must
# reach on it. The corpus is committed so a rejection can be reproduced rather
# than believed, and this script is what regenerates it; a fixture whose bytes
# no longer match what the script writes is a defect in the same way a failing
# test is.
#
# There is no toolchain here that emits ELF for the target, so every specimen
# is built by hand. That is a feature for this job: the malformations wanted
# here -- a segment that runs past the file, a version chain that loops, a
# DT_NEEDED offset past the end of the string table -- are exactly the ones a
# real linker refuses to produce.
#
# Usage:
#   mkfixtures.py [options]
#
# Options:
#   -o DIR, --out=DIR   Corpus directory. [default: corpus]
#   -l, --list          List the fixtures and their verdicts, write nothing.
#   -q, --quiet         Errors only.
#   -h, --help          Print this message and exit.
#
# Each fixture is written as NAME.elf together with a single manifest.tsv whose
# rows are  NAME.elf <TAB> accept | reject <TAB> code <TAB> field-substring.

import os
import struct
import sys

BASE = 0x200000          # link base; vaddr = BASE + file offset in one PT_LOAD
PAGE = 0x1000

EM_X86_64 = 62
ET_EXEC, ET_DYN = 2, 3
PT_LOAD, PT_DYNAMIC, PT_INTERP, PT_TLS, PT_GNU_RELRO = 1, 2, 3, 7, 0x6474e552

DT = {
    'NULL': 0, 'NEEDED': 1, 'HASH': 4, 'STRTAB': 5, 'SYMTAB': 6,
    'STRSZ': 10, 'SYMENT': 11, 'SONAME': 14,
    'VERSYM': 0x6ffffff0, 'VERDEF': 0x6ffffffc, 'VERDEFNUM': 0x6ffffffd,
    'VERNEED': 0x6ffffffe, 'VERNEEDNUM': 0x6fffffff,
}


def p16(v): return struct.pack('<H', v & 0xffff)
def p32(v): return struct.pack('<I', v & 0xffffffff)
def p64(v): return struct.pack('<Q', v & 0xffffffffffffffff)


class Image:
    """A byte buffer under construction, with the field offsets a mutator
    needs to reach recorded as they are laid down."""

    def __init__(self):
        self.buf = bytearray()
        self.meta = {}

    def place(self, key, data):
        off = len(self.buf)
        self.buf += data
        self.meta[key] = off
        return off

    def align(self, n):
        while len(self.buf) % n:
            self.buf += b'\x00'


def strtab():
    """Return (bytes, {name: offset}). Index 0 is the required NUL."""
    names = ['', 'libc.so.6', 'libm.so.6', 'libfoo.so.1',
             'GLIBC_2.2.5', 'GLIBC_2.14', 'FOO_1.0']
    blob = bytearray()
    off = {}
    for n in names:
        off[n] = len(blob)
        blob += n.encode() + b'\x00'
    return bytes(blob), off


def build(kind='full'):
    """Construct one image. kind selects the structural shape; value-level
    malformations are patched onto a 'full' image by the mutators below."""
    img = Image()
    et = ET_DYN if kind != 'nodyn' else ET_EXEC

    # Program headers: a fixed set of slots so their offsets are known. One
    # PT_LOAD covering the whole file, then (optionally) PT_DYNAMIC, then a
    # spare PT_LOAD slot used only by the 'overlap' shape.
    nph = 1
    if kind not in ('nodyn',):
        nph += 1                      # PT_DYNAMIC
    if kind == 'overlap':
        nph += 1                      # second, overlapping PT_LOAD

    ehsize = 64
    phoff = ehsize
    phsz = 56
    body_off = phoff + nph * phsz     # where content begins

    st, snames = strtab()
    sti = 0

    # Symbol table: two 24-byte entries (null + one), content irrelevant to
    # WP-31 beyond its start being in range.
    sym = p32(0) + b'\x00\x00' + p16(0) + p64(0) + p64(0)          # STN_UNDEF
    sym += p32(snames['libc.so.6']) + bytes([0x12, 0]) + p16(1) + p64(0x1000) + p64(4)

    versym = p16(0) + p16(1)          # one Half per symbol

    # verdef: one base version FOO_1.0
    verdef = (p16(1) + p16(1) + p16(1) + p16(1) + p32(0) + p32(20) + p32(0)
              + p32(snames['FOO_1.0']) + p32(0))
    # verneed: need GLIBC_2.2.5 from libc.so.6
    verneed = (p16(1) + p16(1) + p32(snames['libc.so.6']) + p32(16) + p32(0)
               + p32(0) + p16(0) + p16(0) + p32(snames['GLIBC_2.2.5']) + p32(0))

    # Lay the body out and record offsets.
    img.buf += b'\x00' * body_off     # reserve ehdr+phdrs, filled at the end
    img.place('code', b'\x90' * 16)
    img.align(8)
    dyn_off = len(img.buf)

    # Reserve dynamic array space; filled after table offsets are known.
    if kind == 'nodyn':
        dyn_entries = []
    elif kind == 'minimal':
        dyn_entries = ['STRTAB', 'STRSZ', 'NULL']
    else:
        dyn_entries = ['STRTAB', 'STRSZ', 'SYMTAB', 'SYMENT', 'SONAME',
                       'NEEDED', 'NEEDED', 'VERSYM', 'VERDEF', 'VERDEFNUM',
                       'VERNEED', 'VERNEEDNUM', 'NULL']
    dyn_bytes_len = len(dyn_entries) * 16
    img.buf += b'\x00' * dyn_bytes_len

    img.align(8)
    strtab_off = img.place('strtab', st)
    img.align(8)
    symtab_off = img.place('symtab', sym)
    img.align(8)
    versym_off = img.place('versym', versym)
    img.align(8)
    verdef_off = img.place('verdef', verdef)
    img.align(8)
    verneed_off = img.place('verneed', verneed)
    img.align(PAGE)
    end = len(img.buf)

    # Fill the dynamic array now that every table offset is known.
    vals = {
        'STRTAB': BASE + strtab_off, 'STRSZ': len(st),
        'SYMTAB': BASE + symtab_off, 'SYMENT': 24,
        'SONAME': snames['libfoo.so.1'],
        'VERSYM': BASE + versym_off,
        'VERDEF': BASE + verdef_off, 'VERDEFNUM': 1,
        'VERNEED': BASE + verneed_off, 'VERNEEDNUM': 1,
        'NULL': 0,
    }
    needed_iter = iter([snames['libc.so.6'], snames['libm.so.6']])
    dyn_slot = {}
    cur = dyn_off
    for tag in dyn_entries:
        if tag == 'NEEDED':
            v = next(needed_iter)
        else:
            v = vals[tag]
        img.buf[cur:cur + 16] = p64(DT[tag]) + p64(v)
        dyn_slot.setdefault(tag, cur)   # first occurrence; enough for mutators
        if tag == 'NEEDED':
            dyn_slot['NEEDED_last'] = cur
        cur += 16

    # Program headers.
    def phdr(t, off, vaddr, filesz, memsz, flags=5, align=PAGE):
        return (p32(t) + p32(flags) + p64(off) + p64(vaddr) + p64(vaddr)
                + p64(filesz) + p64(memsz) + p64(align))

    phdrs = bytearray()
    load0_slot = phoff
    phdrs += phdr(PT_LOAD, 0, BASE, end, end, flags=7)
    if kind != 'nodyn':
        dyn_len = dyn_bytes_len
        phdrs += phdr(PT_DYNAMIC, dyn_off, BASE + dyn_off, dyn_len, dyn_len, flags=6)
    if kind == 'overlap':
        # a second PT_LOAD whose vaddr range overlaps the first, which begins
        # at BASE and spans the whole file
        phdrs += phdr(PT_LOAD, 0, BASE, PAGE, PAGE, flags=5)
    img.buf[phoff:phoff + len(phdrs)] = phdrs

    # ELF header.
    e = bytearray(64)
    e[0:4] = bytes([0x7f, ord('E'), ord('L'), ord('F')])
    e[4] = 2      # ELFCLASS64
    e[5] = 1      # ELFDATA2LSB
    e[6] = 1      # EV_CURRENT
    e[16:18] = p16(et)
    e[18:20] = p16(EM_X86_64)
    e[20:24] = p32(1)
    e[24:32] = p64(BASE + 0x1000)     # e_entry
    e[32:40] = p64(phoff)
    e[40:48] = p64(0)                 # e_shoff
    e[52:54] = p16(64)                # e_ehsize
    e[54:56] = p16(56)                # e_phentsize
    e[56:58] = p16(nph)               # e_phnum
    e[58:60] = p16(64)                # e_shentsize
    img.buf[0:64] = e

    img.meta.update(dict(
        phoff=phoff, load0=load0_slot, dyn_off=dyn_off, dyn_slot=dyn_slot,
        strtab_off=strtab_off, strsz=len(st), versym_off=versym_off,
        verdef_off=verdef_off, verneed_off=verneed_off, end=end,
        snames=snames, nph=nph))
    return img


# ---- mutators: each returns (bytes, expect) ---------------------------------
# expect is ('accept',) or ('reject', code, field_substring).

def _load0_field(m, name):
    """File offset of a field within the first PT_LOAD program header."""
    base = m['load0']
    off = {'p_type': 0, 'p_offset': 8, 'p_vaddr': 16, 'p_filesz': 32,
           'p_memsz': 40}[name]
    return base + off


def put64(buf, off, v): buf[off:off + 8] = p64(v)
def put16(buf, off, v): buf[off:off + 2] = p16(v)


def f_baseline():
    return bytes(build('full').buf), ('accept',)

def f_nodyn():
    return bytes(build('nodyn').buf), ('accept',)

def f_minimal_dyn():
    return bytes(build('minimal').buf), ('accept',)

def f_trunc_ehdr():
    return bytes(build('full').buf[:40]), ('reject', 'size', 'e_ident')

def f_bad_magic():
    m = build('full'); m.buf[1] = ord('X')
    return bytes(m.buf), ('reject', 'magic', 'EI_MAG')

def f_class32():
    m = build('full'); m.buf[4] = 1
    return bytes(m.buf), ('reject', 'magic', 'EI_CLASS')

def f_not_x86():
    m = build('full'); put16(m.buf, 18, 0x28)
    return bytes(m.buf), ('reject', 'magic', 'e_machine')

def f_type_rel():
    m = build('full'); put16(m.buf, 16, ET_EXEC)   # exec is allowed
    # use ET_REL (1), which is not loadable and must be rejected
    put16(m.buf, 16, 1)
    return bytes(m.buf), ('reject', 'header', 'e_type')

def f_phoff_oob():
    m = build('full'); put64(m.buf, 32, 0xdeadbeef)
    return bytes(m.buf), ('reject', 'phdr', 'e_phoff')

def f_phnum_huge():
    m = build('full'); put16(m.buf, 56, 0x4000)
    return bytes(m.buf), ('reject', 'phdr', 'e_phoff')

def f_phentsize_bad():
    m = build('full'); put16(m.buf, 54, 55)
    return bytes(m.buf), ('reject', 'header', 'e_phentsize')

def f_load_filesz_oob():
    m = build('full'); put64(m.buf, _load0_field(m.meta, 'p_filesz'), 1 << 40)
    return bytes(m.buf), ('reject', 'phdr', 'p_offset')

def f_load_zero_memsz():
    m = build('full'); put64(m.buf, _load0_field(m.meta, 'p_memsz'), 0)
    return bytes(m.buf), ('reject', 'phdr', 'PT_LOAD.p_memsz')

def f_filesz_gt_memsz():
    m = build('full')
    # shrink memsz below filesz on the sole PT_LOAD
    put64(m.buf, _load0_field(m.meta, 'p_memsz'), 32)
    put64(m.buf, _load0_field(m.meta, 'p_filesz'), 64)
    return bytes(m.buf), ('reject', 'phdr', 'PT_LOAD.p_filesz')

def f_load_overlap():
    return bytes(build('overlap').buf), ('reject', 'overlap', 'PT_LOAD.p_vaddr')

def f_dyn_no_null():
    m = build('full')
    off = m.meta['dyn_slot']['NULL']
    put64(m.buf, off, DT['NEEDED']); put64(m.buf, off + 8, 1)  # clobber DT_NULL
    return bytes(m.buf), ('reject', 'dynamic', 'DT_NULL')

def f_needed_past_strtab():
    m = build('full')
    off = m.meta['dyn_slot']['NEEDED']
    put64(m.buf, off + 8, m.meta['strsz'] + 100)
    return bytes(m.buf), ('reject', 'strtab', 'DT_NEEDED')

def f_strtab_oob():
    m = build('full')
    off = m.meta['dyn_slot']['STRTAB']
    put64(m.buf, off + 8, BASE + (1 << 30))   # vaddr not backed by any segment
    return bytes(m.buf), ('reject', 'strtab', 'DT_STRTAB')

def f_strsz_missing():
    m = build('full')
    off = m.meta['dyn_slot']['STRSZ']
    put64(m.buf, off, 0x70000000)             # turn DT_STRSZ into an ignored tag
    return bytes(m.buf), ('reject', 'strtab', 'DT_STRSZ')

def f_syment_bad():
    m = build('full')
    off = m.meta['dyn_slot']['SYMENT']
    put64(m.buf, off + 8, 25)
    return bytes(m.buf), ('reject', 'symtab', 'DT_SYMENT')

def f_versym_oob():
    m = build('full')
    off = m.meta['dyn_slot']['VERSYM']
    put64(m.buf, off + 8, BASE + (1 << 30))
    return bytes(m.buf), ('reject', 'version', 'DT_VERSYM')

def f_verdef_loop():
    m = build('full')
    put64(m.buf, m.meta['dyn_slot']['VERDEFNUM'] + 8, 2)   # claim two records
    # the sole record's vd_next is 0, so the chain cannot reach the second
    return bytes(m.buf), ('reject', 'version', 'vd_next')

def f_verdef_name_oob():
    m = build('full')
    # vda_name sits at verdef_off + 20 (first verdaux)
    put32 = lambda buf, o, v: buf.__setitem__(slice(o, o + 4), p32(v))
    put32(m.buf, m.meta['verdef_off'] + 20, m.meta['strsz'] + 50)
    return bytes(m.buf), ('reject', 'version', 'vda_name')

def f_verneed_loop():
    m = build('full')
    put64(m.buf, m.meta['dyn_slot']['VERNEEDNUM'] + 8, 2)  # claim two records
    return bytes(m.buf), ('reject', 'version', 'vn_next')

def f_verneed_name_oob():
    m = build('full')
    # vna_name sits at verneed_off + 16 (Verneed) + 8 (into Vernaux)
    put32 = lambda buf, o, v: buf.__setitem__(slice(o, o + 4), p32(v))
    put32(m.buf, m.meta['verneed_off'] + 16 + 8, m.meta['strsz'] + 50)
    return bytes(m.buf), ('reject', 'version', 'vna_name')


FIXTURES = [
    ('baseline-good', f_baseline),
    ('static-nodyn', f_nodyn),
    ('minimal-dynamic', f_minimal_dyn),
    ('trunc-ehdr', f_trunc_ehdr),
    ('bad-magic', f_bad_magic),
    ('class-elf32', f_class32),
    ('machine-not-x86', f_not_x86),
    ('type-rel', f_type_rel),
    ('phoff-out-of-range', f_phoff_oob),
    ('phnum-implausible', f_phnum_huge),
    ('phentsize-wrong', f_phentsize_bad),
    ('load-filesz-past-eof', f_load_filesz_oob),
    ('load-zero-memsz', f_load_zero_memsz),
    ('load-filesz-gt-memsz', f_filesz_gt_memsz),
    ('load-overlap', f_load_overlap),
    ('dynamic-no-null', f_dyn_no_null),
    ('needed-past-strtab', f_needed_past_strtab),
    ('strtab-not-backed', f_strtab_oob),
    ('strsz-missing', f_strsz_missing),
    ('syment-wrong', f_syment_bad),
    ('versym-not-backed', f_versym_oob),
    ('verdef-chain-loop', f_verdef_loop),
    ('verdef-name-past-strtab', f_verdef_name_oob),
    ('verneed-chain-loop', f_verneed_loop),
    ('verneed-name-past-strtab', f_verneed_name_oob),
]


def main(argv):
    out = 'corpus'
    do_list = False
    quiet = False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('-h', '--help'):
            sys.stdout.write(__doc__ or ''); return 0
        elif a in ('-l', '--list'):
            do_list = True
        elif a in ('-q', '--quiet'):
            quiet = True
        elif a in ('-o', '--out'):
            i += 1; out = argv[i]
        elif a.startswith('--out='):
            out = a.split('=', 1)[1]
        else:
            sys.stderr.write('mkfixtures: unknown option %s\n' % a); return 2
        i += 1

    rows = []
    blobs = {}
    for name, fn in FIXTURES:
        data, expect = fn()
        blobs[name] = data
        if expect[0] == 'accept':
            rows.append((name + '.elf', 'accept', 'ok', ''))
        else:
            rows.append((name + '.elf', 'reject', expect[1], expect[2]))

    if do_list:
        for r in rows:
            sys.stdout.write('%-32s %-7s %-9s %s\n' % r)
        return 0

    os.makedirs(out, exist_ok=True)
    for name, fn in FIXTURES:
        path = os.path.join(out, name + '.elf')
        with open(path, 'wb') as fh:
            fh.write(blobs[name])
    with open(os.path.join(out, 'manifest.tsv'), 'w', newline='\n') as fh:
        for r in rows:
            fh.write('\t'.join(r) + '\n')
    if not quiet:
        sys.stderr.write('mkfixtures: wrote %d fixtures + manifest to %s\n'
                         % (len(FIXTURES), out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
