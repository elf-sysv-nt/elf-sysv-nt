#!/usr/bin/env python3
"""Fine-grained, drill-down progress for WP-56, computed from git truth.

Every count comes from a committed artifact, never a hand-kept list, so the tree
cannot drift from reality -- the same rule that keeps doc/status/delivered.txt
honest. The sources:

  slices (denominator)  spike/demand-census/results/slice-order.tsv
  symbol -> slice       veneer/wiring/symbol-slice.tsv
  symbol -> bucket      veneer/classification/classification.tsv
  slice wired           veneer/wiring/wire-<slice>.gen.c present
  slice live-crossed    veneer/wiring/t/live-<slice>.sh present
  sigfe class           runtime/exports/cygwin-exports.tsv
  velocity              git log, the 'record the <slice> live crossing' commits
  in flight             git worktree list, a wp/56-<slice>-... branch

The tree:  WP-56 -> slice -> bucket (forward/shim/stub) -> symbol.
Two whole-WP phases bracket the slices: census (done) and acceptance (the
overall done-when, pending). Wiring and live-crossing are per-slice states, not
separate subtrees, so a slice is one node carrying both.

Usage:
  progress.py [NODE ...] [--depth N]
    NODE path:  wp-56  <slice>  <bucket>  <symbol>
      progress.py                     WP-56: phases + every slice, one line each
      progress.py wp-56 stdio         the stdio slice: state + bucket breakdown
      progress.py wp-56 stdio shim    the shim symbols of stdio, with targets
      progress.py wp-56 stdio shim __xstat64    one symbol, in full
    --depth N   expand N levels below the addressed node (default: 1)

  Acceptance (WP-T4 embryo), a second tree over the same classification:
      progress.py accept              both KPIs, per-package table, blocking symbols
      progress.py accept bzip2        one package: verdict, surface, unresolved -> slice
"""
import os, re, sys, subprocess, time, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = re.split(r'/a/wt/', ROOT.replace('\\', '/'))[0]

SLICE_ORDER = os.path.join(REPO, 'spike', 'demand-census', 'results', 'slice-order.tsv')
SYMBOL_SLICE = os.path.join(REPO, 'veneer', 'wiring', 'symbol-slice.tsv')
CLASSIFICATION = os.path.join(REPO, 'veneer', 'classification', 'classification.tsv')
WIRING = os.path.join(REPO, 'veneer', 'wiring')
LIVE = os.path.join(REPO, 'veneer', 'wiring', 't')
EXPORTS = os.path.join(REPO, 'runtime', 'exports', 'cygwin-exports.tsv')

NOT_WIREABLE = {'unassigned', 'dl'}   # residue and the runtime's own job
BUCKET_GROUP = {'1': 'forward', '2': 'forward', '3': 'shim', '4': 'stub'}


def _rows(path):
    try:
        with open(path, encoding='utf-8', errors='replace') as fh:
            for line in fh:
                line = line.rstrip('\n')
                if line and not line.startswith('#'):
                    yield line.split('\t')
    except OSError:
        return


def slices():
    """[slice] in demand order, from the census."""
    return [r[0] for r in _rows(SLICE_ORDER)]


def symbol_slice():
    """symbol -> slice."""
    return {r[0]: r[1] for r in _rows(SYMBOL_SLICE) if len(r) >= 2}


def classification():
    """symbol -> (bucket, disposition, target). One row per symbol; a symbol's
    disposition is the same across its version nodes, so the first non-scaffold
    row wins."""
    out = {}
    for r in _rows(CLASSIFICATION):
        if len(r) < 6:
            continue
        sym, bucket, disp, target = r[1], r[3], r[4], r[5]
        if bucket == 'scaffold':
            continue
        if sym not in out:
            out[sym] = (bucket, disp, target)
    return out


def sigfe_map():
    """runtime export name -> sigfe class (SIGFE / NOSIGFE / ...)."""
    out = {}
    for r in _rows(EXPORTS):
        if len(r) >= 3:
            out[r[0]] = r[2]
    return out


def wired_slices():
    return {os.path.basename(p)[len('wire-'):-len('.gen.c')]
            for p in glob.glob(os.path.join(WIRING, 'wire-*.gen.c'))}


def crossed_slices():
    out = set()
    for p in glob.glob(os.path.join(LIVE, 'live-*.sh')):
        name = os.path.basename(p)[len('live-'):-len('.sh')]
        out.add(name)
    return out


def git(args):
    try:
        return subprocess.check_output(['git', '-C', REPO] + args,
                                       stderr=subprocess.DEVNULL).decode()
    except Exception:
        return ''


def in_flight_slice():
    """The slice a live-cross worktree is on, or None."""
    for line in git(['worktree', 'list']).splitlines():
        m = re.search(r'wp/56-([a-z0-9-]+?)-live-cross', line) or \
            re.search(r'56-live-([a-z0-9-]+)-crossing', line) or \
            re.search(r'wp/56-([a-z0-9-]+)', line)
        if m:
            return m.group(1)
    return None


def crossing_events():
    """[(epoch, slice)] for each recorded live crossing, newest first."""
    out = []
    log = git(['log', '--pretty=%ct\t%s', 'main'])
    for line in log.splitlines():
        if '\t' not in line:
            continue
        ct, subj = line.split('\t', 1)
        m = re.search(r'record the ([a-z0-9-]+) slice live cross', subj)
        if m:
            try:
                out.append((int(ct), m.group(1)))
            except ValueError:
                pass
    return out


def velocity():
    """(rate_per_hour, span_desc, last_ago) over the recent crossings, or None."""
    ev = crossing_events()
    if len(ev) < 2:
        return None
    recent = ev[:min(6, len(ev))]
    newest, oldest = recent[0][0], recent[-1][0]
    span_h = (newest - oldest) / 3600.0
    if span_h <= 0:
        return None
    rate = (len(recent) - 1) / span_h
    last_ago = (time.time() - newest) / 60.0
    return rate, len(recent), last_ago


# ---- model ---------------------------------------------------------------

def build():
    ss = symbol_slice()
    cls = classification()
    sig = sigfe_map()
    wired = wired_slices()
    crossed = crossed_slices()
    order = slices()

    # symbols per slice, grouped by bucket
    per_slice = {s: {'forward': [], 'shim': [], 'stub': [], 'other': []} for s in order}
    for sym, sl in ss.items():
        if sl not in per_slice:
            per_slice.setdefault(sl, {'forward': [], 'shim': [], 'stub': [], 'other': []})
        bkt = cls.get(sym)
        group = BUCKET_GROUP.get(bkt[0], 'other') if bkt else 'other'
        per_slice[sl][group].append(sym)

    model = {'order': order, 'wired': wired, 'crossed': crossed,
             'per_slice': per_slice, 'cls': cls, 'sig': sig,
             'in_flight': in_flight_slice()}
    return model


def slice_sigfe_mix(model, sl):
    """(sigfe, nosigfe) counts over the slice's wired (forward/shim) symbols.
    A fact about how many of the slice's calls route through a Cygwin sigfe
    thunk -- reported for context. It is NOT a gate: crossed slices are
    SIGFE-heavy too, so it does not decide crossability or ETA."""
    cls, sig = model['cls'], model['sig']
    groups = model['per_slice'].get(sl, {})
    sigfe = nosigfe = 0
    for group in ('forward', 'shim'):
        for sym in groups.get(group, []):
            target = cls.get(sym, (None, None, sym))[2] or sym
            klass = sig.get(target) or sig.get(sym)
            if klass == 'NOSIGFE':
                nosigfe += 1
            elif klass and klass.startswith('SIGFE'):
                sigfe += 1
    return sigfe, nosigfe


def counts(model, sl):
    g = model['per_slice'].get(sl, {})
    return {k: len(g.get(k, [])) for k in ('forward', 'shim', 'stub', 'other')}


# ---- rendering -----------------------------------------------------------

def bar(done, total, width=20):
    if total <= 0:
        return '[' + '-' * width + ']'
    n = int(round(width * done / total))
    return '[' + '#' * n + '.' * (width - n) + ']'


def render_top(model, depth):
    order = model['order']
    wireable = [s for s in order if s not in NOT_WIREABLE]
    wired = [s for s in wireable if s in model['wired']]
    crossed = [s for s in wireable if s in model['crossed']]
    infl = model['in_flight']

    print('WP-56 — libc veneer wiring')
    print('  phase census        done   (26 slices ranked by demand)')
    print('  phase wiring        %s %d/%d slices'
          % (bar(len(wired), len(wireable)), len(wired), len(wireable)))
    print('  phase live-crossing %s %d/%d slices%s'
          % (bar(len(crossed), len(wireable)), len(crossed), len(wireable),
             ('  (in flight: %s)' % infl) if infl else ''))
    print('  phase acceptance    pending  (a vendor package builds, links, runs its suite, passes)')

    remaining = [s for s in wireable if s in model['wired'] and s not in model['crossed']]
    v = velocity()
    if v:
        rate, n, ago = v
        eta = ''
        if rate > 0 and remaining:
            eta = '  ~%.1fh to cross the %d wired slices left' % (len(remaining) / rate, len(remaining))
        print('  velocity            ~%.1f slices/hr (last %d, most recent %.0f min ago)%s'
              % (rate, n, ago, eta))

    if depth >= 1:
        print('\n  slice                wired  crossed  buckets (fwd/shim/stub)')
        for s in order:
            if s in NOT_WIREABLE:
                mark = 'n/a (residue)' if s == 'unassigned' else 'n/a (runtime)'
                print('  %-18s  %s' % (s, mark))
                continue
            c = counts(model, s)
            w = 'yes' if s in model['wired'] else ' - '
            if s == infl:
                x = 'IN FLIGHT'
            elif s in model['crossed']:
                x = 'yes'
            else:
                x = ' - '
            print('  %-18s  %-4s   %-9s %3d / %3d / %3d'
                  % (s, w, x, c['forward'], c['shim'], c['stub']))
    print('\n  drill down:  progress.py wp-56 <slice> [<bucket> [<symbol>]]')


def render_slice(model, sl, depth):
    if sl not in model['per_slice']:
        print('no such slice: %s' % sl); return 2
    c = counts(model, sl)
    total = c['forward'] + c['shim'] + c['stub'] + c['other']
    print('WP-56 / %s' % sl)
    print('  wired          %s' % ('yes' if sl in model['wired'] else 'no'))
    infl = model['in_flight']
    print('  live-crossed   %s'
          % ('IN FLIGHT' if sl == infl else ('yes' if sl in model['crossed'] else 'no')))
    if sl in model['wired']:
        sf, nsf = slice_sigfe_mix(model, sl)
        print('  sigfe mix      %d via sigfe thunk, %d direct (context, not a gate)' % (sf, nsf))
    print('  symbols        %d  (%d forward, %d shim, %d stub%s)'
          % (total, c['forward'], c['shim'], c['stub'],
             (', %d other' % c['other']) if c['other'] else ''))
    if depth >= 1:
        print('\n  bucket    count   meaning')
        print('  forward   %5d   resolves to a runtime export (same-name or aliased)' % c['forward'])
        print('  shim      %5d   a runtime export exists but the ABI differs; a translation' % c['shim'])
        print('  stub      %5d   nothing behind it yet; fails predictably' % c['stub'])
        if c['other']:
            print('  other     %5d   no bucket (unclassified / not in the map)' % c['other'])
        print('\n  drill down:  progress.py wp-56 %s <bucket> [<symbol>]' % sl)
    return 0


def render_bucket(model, sl, bucket, depth):
    g = model['per_slice'].get(sl, {})
    if bucket not in ('forward', 'shim', 'stub', 'other'):
        print('bucket must be one of: forward shim stub'); return 2
    syms = sorted(g.get(bucket, []))
    cls = model['cls']
    print('WP-56 / %s / %s   (%d symbols)' % (sl, bucket, len(syms)))
    for sym in syms:
        b = cls.get(sym)
        if b:
            _, disp, target = b
            print('  %-28s %-14s -> %s' % (sym, disp, target))
        else:
            print('  %-28s (unclassified)' % sym)
    return 0


def render_symbol(model, sl, bucket, sym):
    cls, sig = model['cls'], model['sig']
    b = cls.get(sym)
    print('WP-56 / %s / %s / %s' % (sl, bucket, sym))
    if not b:
        print('  unclassified (not in the classification map)'); return 0
    bkt, disp, target = b
    klass = sig.get(target) or sig.get(sym) or '-'
    print('  bucket        %s (%s)' % (bkt, disp))
    print('  target        %s' % target)
    print('  sigfe class   %s' % klass)
    print('  slice         %s' % sl)
    return 0


# ---- acceptance (WP-T4 embryo) -------------------------------------------
#
# The harness (acceptance/accept.sh) builds each pinned package, reads the
# built ELF's undefined libc symbols, and classifies them against the veneer:
# forward and wired (a certified shim) and filled resolve today; shim, stub and
# unclassified do not. Its per-package machine line and its named-symbol block
# are the git truth this view reads. Two KPIs fall out of the same data: a
# checklist (packages by verdict) and a coverage (veneer symbols a package has
# actually exercised), reported against two denominators.

ACCEPT_DIR  = os.path.join(REPO, 'acceptance')
SURFACE_DIR = os.path.join(ACCEPT_DIR, 'surface')
FILLED_GLOB = os.path.join(REPO, 'veneer', 'wiring', '*-filled.tsv')
SHIMS_GLOB  = os.path.join(REPO, 'veneer', 'wiring', 'wire-*.shims.tsv')

RESOLVED = ('forward', 'wired', 'filled')     # the runtime answers these today
BLOCKING = ('shim', 'stub', 'unclassified')   # these keep a package from ready
PASSING  = ('passing', 'green')               # verdicts the run stage will emit


def pinned_packages():
    return [r[0] for r in _rows(os.path.join(ACCEPT_DIR, 'packages.tsv')) if r and r[0]]


def newest_results():
    best = None
    for p in glob.glob(os.path.join(ACCEPT_DIR, 'results-*.txt')):
        if best is None or p > best:          # results-YYYY-MM-DD sorts by date
            best = p
    return best


def parse_results(path):
    """pkg -> {verdict, total, counts{bucket:n}, named{bucket:[sym]}, shape}.
    The machine line carries counts and verdict; the indented block names every
    non-forward symbol (forwards are counted there, not named -- the sidecar is
    what names them)."""
    pkgs, cur, bucket = {}, None, None
    label = [('wired', 'wired'), ('shim', 'shim'), ('stub', 'stub'),
             ('filled', 'filled'), ('unclassified', 'unclassified')]
    if not path:
        return pkgs
    for line in open(path, encoding='utf-8', errors='replace'):
        raw = line.rstrip('\n')
        m = re.match(r'^(?P<name>\S+)=surface:(?P<surface>\d+),forward:(?P<forward>\d+),'
                     r'(?:wired:(?P<wired>\d+),)?shim:(?P<shim>\d+),stub:(?P<stub>\d+),'
                     r'filled:(?P<filled>\d+),unclassified:(?P<unclassified>\d+),'
                     r'(?:shape:(?P<shape>\S+),)?verdict:(?P<verdict>\S+)', raw)
        if m:
            g = m.groupdict()
            d = pkgs.setdefault(g['name'], {'named': {}})
            d['total'] = int(g['surface'])
            d['counts'] = dict(forward=int(g['forward']), wired=int(g['wired'] or 0),
                               shim=int(g['shim']), stub=int(g['stub']),
                               filled=int(g['filled']), unclassified=int(g['unclassified']))
            d['shape'], d['verdict'] = g['shape'] or '-', g['verdict']
            cur = None
            continue
        hm = re.match(r'^(\S+)\s+(ready|needs-wiring|shape-mismatch|does-not-build|passing|green)\s+builds;', raw)
        if hm:
            cur, bucket = pkgs.setdefault(hm.group(1), {'named': {}}), None
            continue
        lm = re.match(r'^    (\w[\w -]*?) *\(', raw)
        if lm and cur is not None:
            key, bucket = lm.group(1).strip().lower(), None
            for pref, name in label:
                if key.startswith(pref):
                    bucket = name; break
            continue
        sm = re.match(r'^      (\S+)$', raw)
        if sm and cur is not None and bucket:
            cur['named'].setdefault(bucket, []).append(sm.group(1))
    return pkgs


def surface_sidecar(pkg):
    """[(symbol, bucket)] from acceptance/surface/<pkg>.tsv, or None. accept.sh
    writes the full classified surface there, forwards included, so coverage can
    count symbol identities rather than only the named non-forwards."""
    p = os.path.join(SURFACE_DIR, '%s.tsv' % pkg)
    if not os.path.isfile(p):
        return None
    return [(r[0], r[1]) for r in _rows(p) if len(r) >= 2]


def resolvable_surface(model):
    """Distinct libc symbols the veneer answers today: forwards (buckets 1, 2),
    filled bodies, and shims a crossed slice has wired. The denominator for
    runtime coverage."""
    syms = {s for s, (b, _, _) in model['cls'].items() if b in ('1', '2')}
    for p in glob.glob(FILLED_GLOB):
        for r in _rows(p):
            if r and r[0]:
                syms.add(r[0])
    for p in glob.glob(SHIMS_GLOB):
        sl = os.path.basename(p)[len('wire-'):-len('.shims.tsv')]
        if sl in model['crossed']:
            for r in _rows(p):
                if r and r[0]:
                    syms.add(r[0])
    return len(syms)


def _slice_of(ss, sym):
    return ss.get(sym, '?')


def render_accept(model):
    ss = symbol_slice()
    pinned = pinned_packages()
    pkgs = parse_results(newest_results())
    counted = {n: d for n, d in pkgs.items() if 'counts' in d}

    def has(v):
        return [n for n, d in counted.items() if d.get('verdict') == v]
    built = [n for n in counted]
    ready, needw = has('ready'), has('needs-wiring')
    shapem, dnb = has('shape-mismatch'), has('does-not-build')
    passing = [n for n, d in counted.items() if d.get('verdict') in PASSING]

    print('Acceptance — WP-T4 embryo')
    print('  done-when: a pinned vendor package builds, links, runs its own suite, and passes')
    print()
    print('  KPI-A  packages   pinned %d · built %d · ready %d · passing %d'
          % (len(pinned), len(built), len(ready), len(passing)))
    tail = []
    if needw:  tail.append('needs-wiring %d' % len(needw))
    if shapem: tail.append('shape-mismatch %d' % len(shapem))
    if dnb:    tail.append('does-not-build %d' % len(dnb))
    unclassified = [p for p in pinned if p not in counted]
    if unclassified: tail.append('not-yet-run %d' % len(unclassified))
    if tail:
        print('                    (%s)' % ', '.join(tail))

    # KPI-B, two denominators.
    dem_total = sum(d['total'] for d in counted.values())
    dem_res = sum(sum(d['counts'][b] for b in RESOLVED) for d in counted.values())
    exercised, missing_sidecar = set(), []
    for n in passing:
        sc = surface_sidecar(n)
        if sc is None:
            missing_sidecar.append(n)
        else:
            exercised.update(s for s, _ in sc)
    R = resolvable_surface(model)
    print()
    if dem_total:
        print('  KPI-B  suite      %d/%d symbol demands across the pinned suite are resolved (%.0f%%)'
              % (dem_res, dem_total, 100.0 * dem_res / dem_total))
    print('         runtime    %d/%d veneer symbols proven by a passing package (%.1f%%)'
          % (len(exercised), R, 100.0 * len(exercised) / R if R else 0.0))
    if not passing:
        print('                    no package passes yet; the suite-run stage is not live, so runtime coverage stays 0')
    elif missing_sidecar:
        print('                    (%s: no surface sidecar yet; rerun acceptance/accept.sh to count its symbols)'
              % ', '.join(missing_sidecar))

    print()
    print('  package       verdict         surface  fwd/wired/shim/stub/filled   shape')
    for n in pinned:
        d = counted.get(n)
        if not d:
            print('  %-12s  %-14s  (pinned, not yet classified)' % (n, '-'))
            continue
        c = d['counts']
        print('  %-12s  %-14s  %-7d  %d / %d / %d / %d / %d          %s'
              % (n, d['verdict'], d['total'], c['forward'], c['wired'],
                 c['shim'], c['stub'], c['filled'], d.get('shape', '-')))

    # Cross-package: which unresolved symbol blocks the most packages.
    blockers = {}
    for n, d in counted.items():
        for b in BLOCKING:
            for sym in d['named'].get(b, []):
                blockers.setdefault(sym, {'pkgs': set(), 'bucket': b})['pkgs'].add(n)
    print()
    if blockers:
        print('  blocking symbols (ranked by packages waiting -> slice that owns the fix):')
        rank = sorted(blockers.items(), key=lambda kv: (-len(kv[1]['pkgs']), kv[0]))
        for sym, info in rank[:20]:
            print('    %-26s %2d pkg  %-6s %s'
                  % (sym, len(info['pkgs']), info['bucket'], _slice_of(ss, sym)))
    else:
        print('  blocking symbols: none — every pinned package resolves against the veneer as it stands')
    print()
    print('  drill down:  progress.py accept <package>')
    return 0


def render_accept_package(model, pkg):
    ss = symbol_slice()
    d = parse_results(newest_results()).get(pkg)
    print('Acceptance / %s' % pkg)
    if not d or 'counts' not in d:
        if pkg in pinned_packages():
            print('  pinned, not yet classified (run acceptance/accept.sh %s)' % pkg)
        else:
            print('  not a pinned package (see acceptance/packages.tsv)')
        return 0
    c = d['counts']
    res = sum(c[b] for b in RESOLVED)
    blk = sum(c[b] for b in BLOCKING)
    print('  verdict     %s' % d['verdict'])
    print('  shape       %s' % d.get('shape', '-'))
    print('  surface     %d libc symbols' % d['total'])
    print('  resolved    %d  (%d forward, %d wired shim, %d filled)'
          % (res, c['forward'], c['wired'], c['filled']))
    print('  blocking    %d  (%d shim, %d stub, %d unclassified)'
          % (blk, c['shim'], c['stub'], c['unclassified']))

    named = d['named']
    if blk:
        print('\n  unresolved symbols, by slice (the wiring each waits on):')
        rows = [(sym, b, _slice_of(ss, sym)) for b in BLOCKING for sym in named.get(b, [])]
        for sym, b, sl in sorted(rows, key=lambda r: (r[2], r[0])):
            print('    %-26s %-6s %s' % (sym, b, sl))
    else:
        print('\n  every symbol resolves. The suite run is next; it waits on the loader\'s')
        print('  dynamic-exec path to stand in for ld-linux, which is WP-56\'s to land.')
    sc = surface_sidecar(pkg)
    if sc:
        print('\n  full surface sidecar present: %d symbols classified in acceptance/surface/%s.tsv' % (len(sc), pkg))
    return 0


def main(argv):
    depth = 1
    path = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('-h', '--help'):
            print(__doc__); return 0
        if a == '--depth':
            i += 1; depth = int(argv[i]) if i < len(argv) else 1
        else:
            path.append(a)
        i += 1

    model = build()

    if path and path[0].lower() == 'accept':
        if len(path) == 1:
            return render_accept(model)
        return render_accept_package(model, path[1])

    # normalise a leading "wp-56"
    if path and path[0].lower() in ('wp-56', 'wp56'):
        path = path[1:]

    if not path:
        render_top(model, depth); return 0
    if len(path) == 1:
        return render_slice(model, path[0], depth)
    if len(path) == 2:
        return render_bucket(model, path[0], path[1], depth)
    return render_symbol(model, path[0], path[1], path[2])


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
