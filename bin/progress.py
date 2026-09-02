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

  Dynamic-exec path (DR-0058), read from the worker's .smon/log, no build:
      progress.py dynexec             driver stages + the crossing certification steps
      progress.py dynexec dyn-cross   one step: the commands it ran, with exit codes

  Done-when clauses, aggregated from the same ledger:
      progress.py clauses             outstanding (unmet, partial) done-when clauses
      progress.py clauses wp-31       clauses whose id matches a substring
      progress.py clauses --all       include the met clauses too

  Road to green (WP-56 completion burndown), status derived from git truth:
      progress.py green               the capabilities between bzip2 ready and passing, N of M

  Acceptance matrix, reusing stored surfaces (no rebuild), recomputed against today's veneer:
      progress.py matrix              pinned packages x resolution buckets, with fingerprint freshness
      progress.py matrix bzip2        one package's stored symbols, resolved now, blocked ones by slice
"""
import os, re, sys, subprocess, time, glob, json, shutil, hashlib


def term_width(default=100):
    """The terminal's column count, for clipping wide lines. Falls back to a
    sane default when output is not a tty (a pipe or a file)."""
    try:
        return shutil.get_terminal_size((default, 24)).columns
    except Exception:
        return default


def _clip(s, width):
    return s if len(s) <= width else s[:max(0, width - 1)] + '…'

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
    dmark, dts = dynexec_state()
    print('  phase dynamic-exec  dyn-cross %s%s   (progress.py dynexec)'
          % (dmark, ('  ' + _ts(dts)) if dts else ''))
    gd, gi, gt = green_summary()
    gtail = (' +%d in flight' % gi) if gi else ''
    print('  phase acceptance    pending  (road to green %d/%d%s — progress.py green)'
          % (gd, gt, gtail))

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
             ('filled', 'filled'), ('unclassified', 'unclassified'),
             ('optional', 'optional')]
    if not path:
        return pkgs
    for line in open(path, encoding='utf-8', errors='replace'):
        raw = line.rstrip('\n')
        m = re.match(r'^(?P<name>\S+)=surface:(?P<surface>\d+),forward:(?P<forward>\d+),'
                     r'(?:wired:(?P<wired>\d+),)?shim:(?P<shim>\d+),stub:(?P<stub>\d+),'
                     r'filled:(?P<filled>\d+),unclassified:(?P<unclassified>\d+),'
                     r'(?:optional:(?P<optional>\d+),)?'
                     r'(?:shape:(?P<shape>\S+),)?verdict:(?P<verdict>\S+)', raw)
        if m:
            g = m.groupdict()
            d = pkgs.setdefault(g['name'], {'named': {}})
            d['total'] = int(g['surface'])
            d['counts'] = dict(forward=int(g['forward']), wired=int(g['wired'] or 0),
                               shim=int(g['shim']), stub=int(g['stub']),
                               filled=int(g['filled']), unclassified=int(g['unclassified']),
                               optional=int(g['optional'] or 0))
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


# ---- dynamic-exec path (DR-0058) -----------------------------------------
#
# The crossing that runs a real dynamic ELF is one driver, loader/exec/dyn_exec.c,
# composing the loader packages already delivered (WP-33/34/35/36/38/39). Its
# progress is not a file count -- the driver is written -- but a certification:
# the loader/exec suite's `dyn-cross` step. session-monitor records every run to
# .smon/log/*.jsonl (one file per run: a `plan` of steps, a `step` event per
# step with ok/fail/skip, an `item` per done-when clause met). This view reads
# that ledger -- what the worker has already certified -- and never builds.

SMON_DIR = os.path.join(REPO, '.smon', 'log')

DYNEXEC_STAGES = [
    ('guard',       "args valid, the image is the dynamic shape, a runtime was supplied"),
    ('add-main',    "the main image enters WP-38's object table as obj[0], the load root"),
    ('add-runtime', "the runtime enters as its one satisfied DT_NEEDED"),
    ('apply',       "the main image's GOT and PLT relocate against the runtime (WP-34/35/36)"),
    ('enter',       "the stub transfers to e_entry, the crossing the static path already owns"),
]

CROSS_STEP = {
    'specimen':  "a bzip2-shaped dynamic image builds",
    'stub':      "the stub composes the crossing between map and entry",
    'dyn-cross': "an interp-bearing image calls across into the runtime and returns",
    'dyn-link':  "the driver links the main image against the runtime (unit over dyn_exec)",
}


def smon_runs(limit=100):
    """[run] newest-first from .smon/log. Each run: {ts, plan, steps{id:state},
    items[(id,state,text)], file}. The last state written for a step id is its
    final one, so a step that started but never finished stays 'start'. limit
    None reads the whole history (clause tracking wants every run, not the
    recent window certification tracking uses)."""
    runs = []
    files = sorted(glob.glob(os.path.join(SMON_DIR, '*.jsonl')), reverse=True)
    for path in (files if limit is None else files[:limit]):
        plan, steps, items, cmds, ts0 = [], {}, [], [], None
        skips = {}
        try:
            fh = open(path, encoding='utf-8', errors='replace')
        except OSError:
            continue
        with fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                try:
                    e = json.loads(line)
                except ValueError:
                    continue
                if ts0 is None:
                    ts0 = e.get('ts')
                ev = e.get('ev')
                if ev == 'plan':
                    plan = e.get('steps', [])
                elif ev == 'step' and e.get('state') in ('ok', 'fail', 'skip', 'start'):
                    sid, st = e.get('id'), e.get('state')
                    steps[sid] = st
                    if st == 'skip':
                        # a skip stands on an earlier run's ok; keep the back-
                        # reference and the input key so latest_step can resolve
                        # it to what was certified, and a reader can see which run
                        # earned the green cell and under which inputs.
                        skips[sid] = {'from': e.get('from'), 'key': e.get('key')}
                elif ev == 'cmd' and e.get('state') in ('ok', 'fail'):
                    cmds.append((e.get('id'), e.get('state'), e.get('rc'), e.get('cmd', '')))
                elif ev == 'item':
                    items.append((e.get('id'), e.get('state'), e.get('text', '')))
        base = os.path.basename(path)
        runs.append({'ts': ts0, 'plan': plan, 'steps': steps, 'items': items,
                     'cmds': cmds, 'file': base, 'skips': skips,
                     'id': os.path.splitext(base)[0]})
    return runs


def _run_by_id(runs, rid):
    """The run whose ledger id (filename stem) is rid, or None. A skip's `from`
    names such an id."""
    if not rid:
        return None
    for r in runs:
        if r.get('id') == rid:
            return r
    return None


def latest_step(runs, step_id):
    """(state, ts, file) for a step id, from the newest run that ran it.

    The incremental suite records a step it did not re-run as a skip carrying a
    `from` back-reference to the run whose ok it stands on. A skip is not a
    result, so follow the reference to the run that earned the ok and report
    that -- its state, and its timestamp, so freshness reflects when the step
    was actually certified rather than when it was last skipped. An unresolvable
    skip (its earning run outside the loaded window, or a broken chain) is not
    trusted: scanning falls through to an older run that ran the step for real."""
    seen = set()
    for r in runs:
        if step_id not in r['steps']:
            continue
        if r['steps'][step_id] != 'skip':
            return r['steps'][step_id], r['ts'], r['file']
        cur = r
        while cur is not None and cur['steps'].get(step_id) == 'skip':
            if cur.get('id') in seen:
                cur = None
                break
            seen.add(cur.get('id'))
            frm = (cur.get('skips', {}).get(step_id) or {}).get('from')
            cur = _run_by_id(runs, frm)
        if cur is not None and step_id in cur['steps'] \
                and cur['steps'][step_id] != 'skip':
            return cur['steps'][step_id], cur['ts'], cur['file']
        # unresolved skip: keep scanning older runs for a real result
    return None, None, None


def crossing_run(runs):
    """The newest run that certified the dynamic crossing (its plan or steps
    name dyn-cross)."""
    for r in runs:
        if 'dyn-cross' in r['plan'] or 'dyn-cross' in r['steps']:
            return r
    return None


def _ts(ts):
    import datetime
    try:
        return datetime.datetime.fromtimestamp(int(ts)).strftime('%Y-%m-%d %H:%M')
    except Exception:
        return '?'


def dynexec_state(runs=None):
    """(mark, ts) for the dyn-cross certification, for the one-line summaries."""
    runs = runs if runs is not None else smon_runs()
    st, ts, _ = latest_step(runs, 'dyn-cross')
    return {'ok': 'certified', 'fail': 'FAILING', 'start': 'running',
            None: 'pending'}.get(st, st or 'pending'), ts


def render_dynexec(model):
    runs = smon_runs()
    print('Dynamic-exec path — the loader crossing that runs a real dynamic ELF')
    print('  DR-0058; loader/exec/dyn_exec.c, one driver over the loader packages')
    print('  already delivered (WP-33 closure, 34 reloc, 35 lookup, 36 version, 38 table, 39 r_debug)')
    print()
    print('  driver stages (committed in dyn_exec.c):')
    for name, desc in DYNEXEC_STAGES:
        print('    %-11s %s' % (name, desc))

    cr = crossing_run(runs)
    print()
    if not cr:
        print('  certification: no dyn-cross run found in .smon/log yet')
        print('  detail: loader/exec/t/run.sh (the smon suite)')
        return 0
    print('  certification — .smon/log/%s, %s (the worker\'s own run, no build):'
          % (cr['file'], _ts(cr['ts'])))
    mark = {'ok': 'ok', 'fail': 'FAIL', 'skip': 'skip', 'start': '…running'}
    for sid in (cr['plan'] or list(cr['steps'])):
        st = cr['steps'].get(sid)
        if st is None:
            continue
        desc = CROSS_STEP.get(sid, '')
        arrow = '  <- the done-when' if sid == 'dyn-cross' else ''
        print('    %-12s %-9s %s%s' % (sid, mark.get(st, st), desc, arrow))

    st, ts, _ = latest_step(runs, 'dyn-cross')
    print()
    if st == 'ok':
        print('  verdict: dyn-cross certifies (%s). The crossing works; running a real' % _ts(ts))
        print('           package\'s own suite through it is the acceptance run stage,')
        print('           which turns KPI-A passing and KPI-B runtime off zero.')
    elif st == 'fail':
        print('  verdict: dyn-cross FAILS as of %s — read the run log below.' % _ts(ts))
    else:
        print('  verdict: dyn-cross not yet certified in the ledger.')
    print()
    print('  drill down:  progress.py dynexec <step>   (the commands a step ran, with exit codes)')
    print('  detail: loader/exec/t/run.sh ; a/build-logs/wp56-*crossing*.log')
    return 0


def render_dynexec_step(model, step):
    """The finest grain the ledger holds: the commands a certification step ran,
    with exit codes, from the newest run that ran it."""
    runs = smon_runs()
    src = None
    for r in runs:
        if step in r['steps'] or any(c[0] == step for c in r['cmds']):
            src = r; break
    print('Dynamic-exec / %s' % step)
    if not src:
        print('  no run in .smon/log has a step named %r' % step)
        print('  known crossing steps: specimen, stub, dyn-cross, unit, fuzz')
        return 0
    st = src['steps'].get(step, '?')
    print('  step verdict   %s   (.smon/log/%s, %s)' % (st, src['file'], _ts(src['ts'])))
    desc = CROSS_STEP.get(step)
    if desc:
        print('  what it proves %s' % desc)
    cmds = [c for c in src['cmds'] if c[0] == step]
    print()
    if not cmds:
        print('  the step recorded no commands (it gated or noted only)')
        return 0
    print('  commands run (exit code):')
    for _id, state, rc, cmd in cmds:
        mark = 'ok' if state == 'ok' else ('FAIL rc=%s' % rc)
        # keep the command readable: drop the long worktree prefix
        short = re.sub(r'/c/-/repo/elf-sysv-nt/a/wt/[^/]+/', '', cmd)
        print('    [%-9s] %s' % (mark, short))
    return 0


# ---- done-when clauses ---------------------------------------------------
#
# Beyond steps, each smon run stamps an `item` per done-when clause it checked,
# with a state: met, partial, or unmet. Those are the plan's "Done when:" prose
# turned into things a suite asserts. This view aggregates them across the whole
# ledger -- the latest state each clause was left in -- so the outstanding
# clauses (unmet, partial) read as the honest "what is left" list, per work
# package. Clause ids are free-form labels the suites chose (WP-31, wp41,
# map.verdict); filter by substring to focus on one.

CLAUSE_MET = ('met', 'ok')
# A failing step stamps a generic item ("a mapping case did not reach its
# expected result") that is a transient failure marker, not a done-when clause.
# It pollutes the outstanding list -- a delivered WP shows a stale unmet -- so
# the clause view drops it and keeps the real, textful clauses.
GENERIC_ITEM = re.compile(r'did not reach its expected result', re.I)


def clause_ledger():
    """{(id, key_text): (state, ts, display_text)} with the latest state each
    clause was left in, over the whole run history. smon_runs is newest-first,
    so the first time a clause is seen is its latest verdict. Generic failure
    stamps are dropped, and digits are normalized for the key so one clause
    re-asserted at different scales (100000 fuzz cases, then 2000000) collapses
    to a single row carrying its latest text."""
    out = {}
    for r in smon_runs(limit=None):
        for cid, state, text in r['items']:
            if not text or GENERIC_ITEM.search(text):
                continue
            key = (cid or '', re.sub(r'\d+', 'N', text))
            if key not in out:
                out[key] = (state, r['ts'], text)
    return out


def render_clauses(model, args):
    filt, show_met = None, False
    for a in args:
        if a in ('--all', '-a', 'met'):
            show_met = True
        else:
            filt = a
    rows = [(cid, disp, st, ts) for (cid, _key), (st, ts, disp) in clause_ledger().items()
            if not filt or filt.lower() in cid.lower()]
    met = [r for r in rows if r[2] in CLAUSE_MET]
    partial = [r for r in rows if r[2] == 'partial']
    unmet = [r for r in rows if r[2] == 'unmet']

    print("Done-when clauses — from .smon/log, the worker's own certification record")
    if filt:
        print('  filter: clause id contains %r' % filt)
    print('  %d clauses recorded (latest state each): %d met, %d partial, %d unmet'
          % (len(rows), len(met), len(partial), len(unmet)))

    def show(title, group):
        if not group:
            return
        print('\n  %s:' % title)
        for cid, txt, st, ts in sorted(group, key=lambda r: (r[0], r[1])):
            t = txt if len(txt) <= 92 else txt[:89] + '...'
            print('    [%-7s] %-24s %s' % (st, cid or '-', t))

    show('unmet', unmet)
    show('partial', partial)
    if show_met:
        show('met', met)
    elif met:
        print('\n  %d met clauses hidden — progress.py clauses --all to list them,'
              ' or clauses <id> to filter' % len(met))
    if not rows:
        print('\n  no clauses match; the ledger records ids like WP-31, wp41, map.verdict')
    return 0


# ---- road to green (WP-56 completion burndown) ---------------------------
#
# The curated ladder from bzip2 `ready` to `passing` lives in
# acceptance/to-green.tsv; each row names a capability and a signal. This view
# evaluates the signal against git truth so the burndown flips itself, and never
# a number by hand.

TO_GREEN = os.path.join(REPO, 'acceptance', 'to-green.tsv')
VERDICT_RANK = {'does-not-build': 0, 'needs-wiring': 1, 'shape-mismatch': 2,
                'ready': 3, 'passing': 4, 'green': 4}


def _met_clause_texts():
    return [disp for (_cid, _k), (st, _ts, disp) in clause_ledger().items()
            if st in CLAUSE_MET]


def _signal_done(sig, runs, results, clauses):
    """True if any comma-separated alternative in `sig` holds against git truth."""
    for alt in sig.split(','):
        alt = alt.strip()
        if not alt or alt == '-':
            continue
        kind, _, rest = alt.partition(':')
        if kind == 'smon':
            step, _, want = rest.partition(':')
            st, _ts, _f = latest_step(runs, step)
            if st == (want or 'ok'):
                return True
        elif kind == 'accept':
            pkg, _, want = rest.partition(':')
            d = results.get(pkg)
            if d and VERDICT_RANK.get(d.get('verdict'), -1) >= VERDICT_RANK.get(want, 99):
                return True
        elif kind == 'clause':
            needle = rest.lower()
            if any(needle in (t or '').lower() for t in clauses):
                return True
        elif kind == 'grep':
            relpath, _, rx = rest.partition(':')
            try:
                text = open(os.path.join(REPO, relpath), encoding='utf-8', errors='replace').read()
                if re.search(rx, text, re.MULTILINE):
                    return True
            except OSError:
                pass
        elif kind == 'run':
            # The rung is done when bzip2's newest committed run-stage line matches
            # -- the integration truth itself, so a rung keys on the run advancing
            # past its obstacle rather than on a proxy.
            note = run_stage_note()
            if note and re.search(rest, note):
                return True
    return False


def road_to_green():
    """[(id, done, signal, desc)] in file order, status derived from git truth."""
    rows = [r for r in _rows(TO_GREEN) if len(r) >= 3 and r[0] != 'id']
    runs = smon_runs()
    results = parse_results(newest_results())
    clauses = _met_clause_texts()
    out = []
    for r in rows:
        out.append((r[0], _signal_done(r[1], runs, results, clauses), r[1], r[2]))
    return out


def active_worktree_slugs():
    """The branch slug of every live session worktree, from git. This is where
    the worker's in-flight work lives before it lands on march, so an open road
    item whose capability is being built right now shows as in-flight rather than
    simply not-started -- current, without counting unlanded work as done."""
    out = []
    for line in git(['worktree', 'list', '--porcelain']).splitlines():
        if line.startswith('branch '):
            out.append(line[len('branch '):].rsplit('/', 1)[-1])
    return out


def _item_inflight(cid, slugs):
    tok = cid.replace('-bringup', '').replace('-wired', '')
    return any(cid in s or tok in s for s in slugs)


def road_with_state():
    """[(id, state, signal, desc)] where state is done / inflight / open. Done is
    landed truth (march); inflight is read from the worktrees."""
    slugs = active_worktree_slugs()
    out = []
    for cid, done, sig, desc in road_to_green():
        state = 'done' if done else ('inflight' if _item_inflight(cid, slugs) else 'open')
        out.append((cid, state, sig, desc))
    return out


def green_summary():
    road = road_with_state()
    done = sum(1 for _i, s, _g, _x in road if s == 'done')
    infl = sum(1 for _i, s, _g, _x in road if s == 'inflight')
    return done, infl, len(road)


def wp56_delivered():
    try:
        return any(l.strip() in ('WP-56', '56')
                   for l in open(os.path.join(REPO, 'doc', 'status', 'delivered.txt'),
                                 encoding='utf-8', errors='replace'))
    except OSError:
        return False


def run_stage_note(pkg='bzip2'):
    """The newest committed run-stage line -- where the package's live launch
    actually halts, or that it ran -- from acceptance/results-*.txt. This is the
    integration truth beneath the rungs: it advances on its own as the worker
    clears each obstacle, even before a rung has a completion signal to flip on."""
    p = newest_results()
    if not p:
        return None
    try:
        txt = open(p, encoding='utf-8', errors='replace').read()
    except OSError:
        return None
    notes = re.findall(r'run stage:\s*(.+)', txt)
    return notes[-1].strip() if notes else None


def render_green(model):
    road = road_with_state()
    done = sum(1 for _i, s, _g, _x in road if s == 'done')
    infl = sum(1 for _i, s, _g, _x in road if s == 'inflight')
    tail = ('  (+%d in flight)' % infl) if infl else ''
    print('Road to green — bzip2 ready -> passing, which is WP-56\'s overall done-when')
    print('  %d of %d capabilities in place%s (acceptance/to-green.tsv; status derived, not hand-set)'
          % (done, len(road), tail))
    print()
    w = term_width()
    note = run_stage_note()
    if note:
        print(_clip('  bzip2 now: %s' % note, w))
        print()
    box = {'done': '[x]', 'inflight': '[~]', 'open': '[ ]'}
    for cid, state, sig, desc in road:
        print(_clip('  %s %-18s %s' % (box[state], cid, desc), w))
        if state == 'inflight':
            print(_clip('  %-22s in flight in a worktree now, not yet landed' % '', w))
        elif state == 'open' and sig and sig != '-':
            print(_clip('  %-22s flips on: %s' % ('', sig), w))
    print()
    deliv = wp56_delivered()
    if done == len(road) and deliv:
        print('  WP-56 is complete: every capability in place and WP-56 in delivered.txt')
    else:
        print('  WP-56 completes when bzip2-passes flips and WP-56 lands in delivered.txt (now: %s)'
              % ('yes' if deliv else 'not yet'))
    return 0


# ---- acceptance matrix (packages x veneer resolution, reusing sidecars) ---
#
# A package's undefined-symbol surface is stable for its pinned version and
# expensive to get (a cross-build). accept.sh persists it to
# acceptance/surface/<pkg>.tsv with a veneer fingerprint. This view reuses those
# stored surfaces -- no rebuild -- and recomputes each symbol's resolution
# against TODAY's veneer, so a cell re-flips when a shim is wired without
# rebuilding the package. The fingerprint says whether a stored surface's own
# verdict still matches the current veneer or has gone stale.


def veneer_fingerprint(model):
    """The resolution inputs -- the classification map and the crossed-slice set
    -- hashed the same way acceptance/accept.sh does, so the two agree."""
    try:
        ch = hashlib.sha256(open(CLASSIFICATION, 'rb').read()).hexdigest()
    except OSError:
        ch = ''
    cr = ','.join(sorted(model['crossed']))
    return hashlib.sha256(('%s|%s' % (ch, cr)).encode()).hexdigest()[:12]


def surface_fingerprint(pkg):
    """The veneer fingerprint stamped in a package's sidecar, or None."""
    p = os.path.join(SURFACE_DIR, '%s.tsv' % pkg)
    try:
        for line in open(p, encoding='utf-8', errors='replace'):
            m = re.match(r'#\s*veneer-fingerprint\s+(\S+)', line)
            if m:
                return m.group(1)
            if not line.startswith('#'):
                break
    except OSError:
        pass
    return None


def _filled_syms():
    s = set()
    for p in glob.glob(FILLED_GLOB):
        for r in _rows(p):
            if r and r[0]:
                s.add(r[0])
    return s


def _wired_shim_syms(model):
    s = set()
    for p in glob.glob(SHIMS_GLOB):
        sl = os.path.basename(p)[len('wire-'):-len('.shims.tsv')]
        if sl in model['crossed']:
            for r in _rows(p):
                if r and r[0]:
                    s.add(r[0])
    return s


def classify_now(sym, cls, filled, wired):
    """A symbol's resolution against the current veneer: forward, wired, filled,
    shim, stub, or unclassified. Mirrors accept.sh's classify order."""
    if sym in filled:
        return 'filled'
    b = cls.get(sym)
    if not b:
        return 'unclassified'
    bucket = b[0]
    if bucket in ('1', '2'):
        return 'forward'
    if bucket == '3':
        return 'wired' if sym in wired else 'shim'
    if bucket == '4':
        return 'stub'
    return 'unclassified'


def package_matrix(pkg, model, filled, wired):
    """Recompute a stored surface against today's veneer. Returns None when no
    sidecar exists, else {counts, blocked[(sym,status,slice)], fresh, stored_fp}."""
    sc = surface_sidecar(pkg)
    if sc is None:
        return None
    ss = symbol_slice()
    counts = {k: 0 for k in ('forward', 'wired', 'filled', 'shim', 'stub', 'unclassified')}
    blocked = []
    for sym, _stored_bucket in sc:
        status = classify_now(sym, model['cls'], filled, wired)
        counts[status] += 1
        if status in ('shim', 'stub', 'unclassified'):
            blocked.append((sym, status, ss.get(sym, '?')))
    stored = surface_fingerprint(pkg)
    return {'counts': counts, 'total': len(sc), 'blocked': blocked,
            'stored_fp': stored, 'fresh': stored == veneer_fingerprint(model)}


def render_matrix(model, args):
    pkg = args[0] if args else None
    filled, wired = _filled_syms(), _wired_shim_syms(model)
    cur = veneer_fingerprint(model)
    if pkg:
        return render_matrix_package(model, pkg, filled, wired, cur)
    w = term_width()
    print('Acceptance matrix — pinned packages x veneer resolution (reuses stored surfaces, no rebuild)')
    print('  current veneer fingerprint: %s' % cur)
    print()
    print('  %-12s %5s  %4s %5s %4s %4s %6s %7s  %-11s %s'
          % ('package', 'surf', 'fwd', 'wired', 'shim', 'stub', 'filled', 'unclass',
             'resolves', 'sidecar'))
    any_pkg = False
    for name in pinned_packages():
        m = package_matrix(name, model, filled, wired)
        if m is None:
            print('  %-12s  (no stored surface — run acceptance/accept.sh %s)' % (name, name))
            continue
        any_pkg = True
        c = m['counts']
        blockers = c['shim'] + c['stub'] + c['unclassified']
        resolves = 'yes' if blockers == 0 else 'no (%d)' % blockers
        tag = 'fresh' if m['fresh'] else ('stale %s' % (m['stored_fp'] or '?'))
        print(_clip('  %-12s %5d  %4d %5d %4d %4d %6d %7d  %-11s %s'
                    % (name, m['total'], c['forward'], c['wired'], c['shim'],
                       c['stub'], c['filled'], c['unclassified'], resolves, tag), w))
    print()
    if not any_pkg:
        print('  no stored surfaces yet. accept.sh writes acceptance/surface/<pkg>.tsv on each run;')
        print('  once committed, this matrix recomputes them against the current veneer without a rebuild.')
    else:
        print('  a stale row: the surface is trusted, but its resolution was recomputed against the')
        print('  current veneer, which has moved since the run. progress.py matrix <pkg> for symbols.')
    return 0


def render_matrix_package(model, pkg, filled, wired, cur):
    m = package_matrix(pkg, model, filled, wired)
    print('Acceptance matrix / %s' % pkg)
    if m is None:
        if pkg in pinned_packages():
            print('  no stored surface — run acceptance/accept.sh %s to write its sidecar' % pkg)
        else:
            print('  not a pinned package (see acceptance/packages.tsv)')
        return 0
    c = m['counts']
    print('  surface     %d symbols (stored; reused without a rebuild)' % m['total'])
    print('  resolution  %d forward, %d wired, %d filled | %d shim, %d stub, %d unclassified'
          % (c['forward'], c['wired'], c['filled'], c['shim'], c['stub'], c['unclassified']))
    print('  fingerprint %s (%s)' % (m['stored_fp'] or '-',
                                     'fresh' if m['fresh'] else 'stale; veneer now %s' % cur))
    if m['blocked']:
        w = term_width()
        print('\n  blocked symbols, by slice:')
        for sym, status, sl in sorted(m['blocked'], key=lambda r: (r[2], r[0])):
            print(_clip('    %-26s %-6s %s' % (sym, status, sl), w))
    else:
        print('\n  every stored symbol resolves against the current veneer')
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

    if path and path[0].lower() == 'matrix':
        return render_matrix(model, path[1:])

    if path and path[0].lower() in ('green', 'road', 'to-green'):
        return render_green(model)

    if path and path[0].lower() in ('clauses', 'done-when', 'donewhen'):
        return render_clauses(model, path[1:])

    if path and path[0].lower() in ('dynexec', 'dyn'):
        if len(path) == 1:
            return render_dynexec(model)
        return render_dynexec_step(model, path[1])

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
