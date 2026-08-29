#!/usr/bin/env python3
#
# Synthesize the static ELF image spike 2 maps.
#
# The spike asks whether a PE stub can map a static ELF and transfer control
# to it. Answering that wants an ELF, and this machine has no toolchain that
# emits one: Cygwin's binutils targets PE and nothing here cross-compiles to
# Linux. So the specimen is built by hand, an ET_EXEC image with the geometry
# a static el8 binary has, carrying a flat blob of code assembled separately
# and handed in with --code.
#
# Hand-built means the geometry is a choice rather than an observation, so the
# choices are named. Three PT_LOADs, R+X then R then R+W, because that is what
# a static binary linked by a modern ld carries. Segment addresses congruent
# to their file offsets modulo p_align, because that is the rule the format
# states and the arithmetic the stub has to get right. A p_memsz larger than
# p_filesz on the writable segment, because .bss is where a loader that
# forgets to zero is caught.
#
# Usage:
#   make-elf.py [options] --code=FILE --output=FILE
#
# Options:
#   -c FILE, --code=FILE      Flat payload blob; becomes the text segment.
#   -o FILE, --output=FILE    ELF destination.
#   -b ADDR, --base=ADDR      Link base. [default: 0x400000]
#   -a N, --align=N           p_align on every PT_LOAD. [default: 0x1000]
#   -z N, --bss=N             Bytes past the end of the file image. [default: 0x2000]
#   -m FILE, --manifest=FILE  Write the resulting addresses here as key=value.
#   -t, --terse               Print the manifest to stdout and nothing else.
#   -q, --quiet               Errors only.
#   -v, --verbose             Describe each segment as it is laid down.
#   -d, --debug               Trace execution; implies --verbose.
#   -V, --version             Print the version and exit.
#   -h, --help                Print this message and exit.
#
# Each option is also settable as MAKE_ELF_<OPTION>, and the option wins over
# the variable.

import os
import struct
import sys

PROG = 'make-elf'
RELEASE = 'make-elf 1.0'

# The three addresses the stub hands the payload. None of them is passed in a
# manifest, because the stub is supposed to derive them from the program
# headers exactly as a loader would: the read-only constant sits at the start
# of the read-only segment, the handshake block at the start of the writable
# one, and the .bss probe at the first byte past that segment's file image.
RODATA_WORD = 0xC0FFEE0FBADC0DE5
HANDSHAKE_BYTES = 0x80

ELFCLASS64, ELFDATA2LSB, EV_CURRENT = 2, 1, 1
ET_EXEC, EM_X86_64 = 2, 62
PT_LOAD, PT_GNU_STACK = 1, 0x6474E551
PF_X, PF_W, PF_R = 1, 2, 4

EHDR_SIZE, PHDR_SIZE, PHDR_COUNT = 64, 56, 4
PAGE = 0x1000


def die(msg):
    sys.stderr.write('%s: %s\n' % (PROG, msg))
    raise SystemExit(1)


def refuse(msg):
    sys.stderr.write('%s: %s\n' % (PROG, msg))
    raise SystemExit(2)


def usage():
    with open(__file__) as handle:
        showing = False
        for line in handle:
            if line.startswith('# Usage:'):
                showing = True
            elif showing and not line.startswith('#'):
                break
            if showing:
                sys.stdout.write(line[2:] if len(line) > 2 else '\n')


def number(text, what):
    try:
        return int(text, 0)
    except ValueError:
        refuse('%s wants a number, got %s' % (what, text))


def env(name, default):
    return os.environ.get('MAKE_ELF_' + name, default)


VALUED = {'-c': 'code', '--code': 'code',
          '-o': 'output', '--output': 'output',
          '-b': 'base', '--base': 'base',
          '-a': 'align', '--align': 'align',
          '-z': 'bss', '--bss': 'bss',
          '-m': 'manifest', '--manifest': 'manifest'}


def parse(argv):
    opt = {'code': env('CODE', ''), 'output': env('OUTPUT', ''),
           'base': env('BASE', '0x400000'), 'align': env('ALIGN', '0x1000'),
           'bss': env('BSS', '0x2000'), 'manifest': env('MANIFEST', ''),
           'terse': env('TERSE', '0') == '1', 'quiet': env('QUIET', '0') == '1',
           'verbose': int(env('VERBOSE', '0')), 'debug': env('DEBUG', '0') == '1'}
    index = 0
    while index < len(argv):
        arg = argv[index]
        if arg in ('-h', '--help'):
            usage()
            raise SystemExit(0)
        if arg in ('-V', '--version'):
            sys.stdout.write(RELEASE + '\n')
            raise SystemExit(0)
        if arg == '--':
            index += 1
            break
        if arg.startswith('--') and '=' in arg:
            name, _, value = arg.partition('=')
            if name not in VALUED:
                refuse('unknown option %s' % name)
            opt[VALUED[name]] = value
        elif arg in VALUED:
            if index + 1 >= len(argv):
                refuse('%s wants a value' % arg)
            opt[VALUED[arg]] = argv[index + 1]
            index += 1
        elif arg in ('-t', '--terse'):
            opt['terse'] = True
        elif arg in ('-q', '--quiet'):
            opt['quiet'] = True
        elif arg in ('-v', '--verbose'):
            opt['verbose'] += 1
        elif arg in ('-d', '--debug'):
            opt['debug'] = True
            opt['verbose'] += 1
        elif arg.startswith('-') and arg != '-':
            refuse('unknown option %s' % arg)
        else:
            break
        index += 1
    if index != len(argv):
        refuse('takes no arguments, got %s' % argv[index])
    return opt


def phdr(p_type, flags, off, vaddr, filesz, memsz, align):
    return struct.pack('<IIQQQQQQ', p_type, flags, off, vaddr, vaddr,
                       filesz, memsz, align)


def page_up(value):
    return (value + PAGE - 1) & ~(PAGE - 1)


def build(code, base, align, bss):
    if base % PAGE:
        die('the link base wants page alignment, got 0x%x' % base)
    if align < PAGE or align & (align - 1):
        die('--align wants a power of two no smaller than 0x1000, got 0x%x' % align)

    headers = EHDR_SIZE + PHDR_COUNT * PHDR_SIZE
    rodata = struct.pack('<Q', RODATA_WORD)
    handshake = b'\0' * HANDSHAKE_BYTES

    # Segment i sits at file offset off and virtual address
    # base + i * align + off. That satisfies p_vaddr == p_offset (mod p_align)
    # without punching align-sized holes in the file, which a naive
    # p_offset == p_vaddr - base would do and which at a 2 MB alignment would
    # mean a four megabyte file to carry a few hundred bytes.
    text_off = 0
    ro_off = page_up(headers + len(code))
    rw_off = page_up(ro_off + len(rodata))

    seg = [
        {'name': 'text', 'flags': PF_R | PF_X, 'off': text_off,
         'vaddr': base + 0 * align + text_off,
         'filesz': headers + len(code), 'memsz': headers + len(code)},
        {'name': 'rodata', 'flags': PF_R, 'off': ro_off,
         'vaddr': base + 1 * align + ro_off,
         'filesz': len(rodata), 'memsz': len(rodata)},
        {'name': 'data', 'flags': PF_R | PF_W, 'off': rw_off,
         'vaddr': base + 2 * align + rw_off,
         'filesz': len(handshake), 'memsz': len(handshake) + bss},
    ]
    entry = seg[0]['vaddr'] + headers

    phdrs = b''.join(phdr(PT_LOAD, s['flags'], s['off'], s['vaddr'],
                          s['filesz'], s['memsz'], align) for s in seg)
    # Every el8 binary carries one, and the stack the stub allocates is
    # expected to honor it. Zero-sized, as a real linker emits it.
    phdrs += phdr(PT_GNU_STACK, PF_R | PF_W, 0, 0, 0, 0, 0x10)

    # EI_OSABI stays ELFOSABI_NONE. WP-10 decides what this byte becomes and
    # nothing in this spike is entitled to guess ahead of it.
    ident = b'\x7fELF' + bytes([ELFCLASS64, ELFDATA2LSB, EV_CURRENT]) + b'\0' * 9
    ehdr = struct.pack('<16sHHIQQQIHHHHHH', ident, ET_EXEC, EM_X86_64,
                       EV_CURRENT, entry, EHDR_SIZE, 0, 0, EHDR_SIZE,
                       PHDR_SIZE, PHDR_COUNT, 0, 0, 0)

    image = bytearray(ehdr + phdrs + code)
    image += b'\0' * (ro_off - len(image))
    image += rodata
    image += b'\0' * (rw_off - len(image))
    image += handshake

    span_first = seg[0]['vaddr']
    span_last = seg[-1]['vaddr'] + seg[-1]['memsz']
    manifest = [('base', '0x%x' % base), ('align', '0x%x' % align),
                ('entry', '0x%x' % entry),
                ('span_first', '0x%x' % span_first),
                ('span_last', '0x%x' % span_last),
                ('span_bytes', '0x%x' % (span_last - span_first)),
                ('rodata_addr', '0x%x' % seg[1]['vaddr']),
                ('rodata_word', '0x%x' % RODATA_WORD),
                ('handshake_addr', '0x%x' % seg[2]['vaddr']),
                ('bss_addr', '0x%x' % (seg[2]['vaddr'] + seg[2]['filesz'])),
                ('bss_bytes', '0x%x' % bss),
                ('code_bytes', '%d' % len(code)),
                ('file_bytes', '%d' % len(image))]
    return bytes(image), seg, entry, manifest


def main(argv):
    opt = parse(argv)
    if opt['debug']:
        sys.stderr.write('%s: %r\n' % (PROG, opt))
    if not opt['code']:
        refuse('nothing to carry. Give --code FILE.')
    if not opt['output']:
        refuse('nowhere to write. Give --output FILE.')

    base = number(opt['base'], '--base')
    align = number(opt['align'], '--align')
    bss = number(opt['bss'], '--bss')

    try:
        with open(opt['code'], 'rb') as handle:
            code = handle.read()
    except IOError as exc:
        die('cannot read the payload: %s' % exc)
    if not code:
        die('the payload is empty: %s' % opt['code'])

    image, seg, entry, manifest = build(code, base, align, bss)

    try:
        with open(opt['output'], 'wb') as handle:
            handle.write(image)
    except IOError as exc:
        die('cannot write %s: %s' % (opt['output'], exc))

    text = ''.join('%s=%s\n' % pair for pair in manifest)
    if opt['manifest']:
        try:
            with open(opt['manifest'], 'w') as handle:
                handle.write(text)
        except IOError as exc:
            die('cannot write %s: %s' % (opt['manifest'], exc))

    if opt['terse']:
        sys.stdout.write(text)
        return 0
    if opt['verbose']:
        for one in seg:
            sys.stdout.write(
                '  %-6s vaddr 0x%09x  off 0x%05x  filesz 0x%05x  memsz 0x%05x  %s%s%s\n'
                % (one['name'], one['vaddr'], one['off'], one['filesz'],
                   one['memsz'],
                   'r' if one['flags'] & PF_R else '-',
                   'w' if one['flags'] & PF_W else '-',
                   'x' if one['flags'] & PF_X else '-'))
    if not opt['quiet']:
        sys.stderr.write('%s: %s, %d bytes, entry 0x%x\n'
                         % (PROG, opt['output'], len(image), entry))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
