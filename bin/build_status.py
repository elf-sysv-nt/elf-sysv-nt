#!/usr/bin/env python3
"""The single source of truth for elf-sysv-nt build status.

`inflight()` classifies every not-yet-merged work-package branch into one state
each — BUILDING, STARTED (scaffold only), committed-idle, or idle — from git
alone. `main()` prints a full status report and a stall verdict.

Two consumers share this one definition:
  - bin/refresh-next-steps.py imports inflight() for the Live-worktrees block.
  - the heartbeat scheduled task runs this file and relays its output.

Read-only: it never writes or commits. Locate it by its own path, so it works
from any working directory.
"""
import glob, os, re, time, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # the worktree root
REPO = re.split(r'/a/wt/', ROOT.replace('\\', '/'))[0]              # the main checkout (holds the lock)
PLAN   = os.path.join(ROOT, 'doc', 'IMPLEMENTATION-PLAN.md')
LOCK   = os.path.join(REPO, 'a', '.build-worker.lock')
LEDGER = os.path.join(ROOT, 'doc', 'status', 'delivered.txt')  # tracked status, never the plan
HOLD   = os.path.join(ROOT, 'doc', 'status', 'hold.txt')       # WPs set aside from autonomous build


def git(args, cwd=ROOT):
    try:
        return subprocess.check_output(['git', '-C', cwd] + args,
                                       stderr=subprocess.DEVNULL).decode()
    except Exception:
        return ''


def recent_files(path, cutoff):
    """Count files under path with mtime strictly after `cutoff` (epoch secs),
    skipping .git. Callers pass the later of the branch's last-commit time and a
    recency floor, so a fresh `git worktree add` (which stamps every checked-out
    file with the current time, but always before the scaffold commit) does not
    read as build activity."""
    n = 0
    for root, dirs, files in os.walk(path):
        if '.git' in root.replace('\\', '/').split('/'):
            dirs[:] = []
            continue
        for f in files:
            try:
                if os.path.getmtime(os.path.join(root, f)) > cutoff:
                    n += 1
            except OSError:
                pass
    return n


FAIL_RE = re.compile(r'^(make(\[\d+\])?: \*\*\* .* Error \d+'
                     r'|configure: error:'
                     r'|\S+: make failed\b)')


def newest_log(branch):
    """Path of the branch's newest build log under a/build-logs/, or None.
    Long builds run outside the worktree (nohup into the log), so the log is
    the only sign the package is still compiling -- or that it broke."""
    m = re.match(r'wp(\d+)', branch)
    if not m:
        return None
    best, best_t = None, None
    for p in glob.glob(os.path.join(REPO, 'a', 'build-logs', 'wp%s-*.log' % m.group(1))):
        try:
            t = os.path.getmtime(p)
        except OSError:
            continue
        if best_t is None or t > best_t:
            best, best_t = p, t
    return best


def log_failed(path, lines=15):
    """True when the log's last lines carry a fatal make/configure error.
    A build that died stops appending, so the error stays at the tail; a
    build that is still running has fresh compile output there instead."""
    try:
        with open(path, 'rb') as f:
            f.seek(0, os.SEEK_END)
            f.seek(max(0, f.tell() - 8192))
            tail = f.read().decode(errors='replace').splitlines()[-lines:]
    except OSError:
        return False
    return any(FAIL_RE.match(l) for l in tail)


def inflight():
    """[(branch_name, state, wp_id)] for each WP branch not yet merged to march."""
    mtip = git(['rev-parse', 'march']).strip()
    pairs, cur = [], None
    for line in git(['worktree', 'list', '--porcelain']).splitlines():
        if line.startswith('worktree '):
            cur = line[len('worktree '):]
        elif line.startswith('branch '):
            name = line[len('branch '):].rsplit('/', 1)[-1]
            if cur and name not in ('march', 'main'):
                pairs.append((name, cur))
    out = []
    for name, wt in pairs:
        tip = git(['rev-parse', 'HEAD'], wt).strip()
        if not tip or tip == mtip:
            continue
        if subprocess.call(['git', '-C', wt, 'merge-base', '--is-ancestor', tip, mtip],
                           stderr=subprocess.DEVNULL) == 0:
            continue                                        # already merged
        ahead = git(['rev-list', '--count', 'HEAD', '^march'], wt).strip() or '0'
        log = [l for l in git(['log', '--oneline', mtip + '..HEAD'], wt).splitlines() if l.strip()]
        scaffold_only = len(log) == 1 and 'scaffold' in log[0].lower()
        tipct = git(['log', '-1', '--format=%ct'], wt).strip()
        cutoff = max(int(tipct), time.time() - 900) if tipct.isdigit() else time.time() - 900
        r = recent_files(wt, cutoff)
        lp = newest_log(name)
        age = None
        if lp:
            try:
                age = time.time() - os.path.getmtime(lp)
            except OSError:
                lp = None
        if lp and log_failed(lp):
            st = 'FAILED (%s, %s commit(s))' % (os.path.basename(lp), ahead)
        elif age is not None and age < 360:
            st = 'BUILDING (log written %ds ago, %s commit(s))' % (int(age), ahead)
        elif r > 0:
            st = 'BUILDING (%s files since last commit, %s commit(s))' % (r, ahead)
        elif scaffold_only:
            st = 'STARTED (scaffold only, not built)'
        elif int(ahead) > 0:
            st = 'committed %s, idle (check: done or stalled)' % ahead
        else:
            st = 'idle'
        m = re.match(r'wp(\d+)', name)
        out.append((name, st, 'WP-%s' % m.group(1) if m else ''))
    return out


def _read_ids(path):
    try:
        return [l.strip() for l in open(path, encoding='utf-8', errors='replace')
                if l.strip() and not l.lstrip().startswith('#')]
    except Exception:
        return []


def delivered_wps():
    """The set of delivered WP ids, from the git-side ledger a/delivered.txt.
    Status lives in the ledger, never in the plan."""
    return set(_read_ids(LEDGER))


def held_wps():
    """WP ids the operator set aside (a/build-hold.txt): not built autonomously,
    but not delivered either."""
    return set(_read_ids(HOLD))


def plan_wps():
    """[(wp_id, [needs])] for every WP in the plan, in plan order. Structure
    only -- the plan is never read for status."""
    try:
        text = open(PLAN, encoding='utf-8', errors='replace').read()
    except Exception:
        return []
    parts = re.split(r'(?m)^### (WP-[0-9A-Za-z]+)\b', text)
    out = []
    for i in range(1, len(parts), 2):
        body = parts[i + 1]
        m = re.search(r'(?m)^Needs:\s*(.+?)\.?\s*$', body)
        needs = re.findall(r'WP-[0-9A-Za-z]+', m.group(1)) if m else []
        if m and re.search(r'\bnone\b', m.group(1), re.I):
            needs = []
        out.append((parts[i], needs))
    return out


def undelivered_wps():
    d = delivered_wps()
    return [w for w, _ in plan_wps() if w not in d]


def next_buildable():
    """First plan WP (in order) that is undelivered, not held, and whose Needs
    are all delivered. None when nothing is buildable right now."""
    d, h = delivered_wps(), held_wps()
    for w, needs in plan_wps():
        if w in d or w in h:
            continue
        if all(n in d for n in needs):
            return w
    return None


def delivered_count():
    return len(delivered_wps())


def last_commit_age():
    """Seconds since the newest commit on march, or None. The heartbeat commits
    every ~5 min, so a large age means the worker task itself is not running."""
    out = git(['log', '-1', '--format=%ct', 'march']).strip()
    try:
        return int(time.time()) - int(out)
    except Exception:
        return None


def lock_age():
    """Seconds since the build-worker lock was taken, or None if free or
    unreadable. The lock directory holds a ts file with the epoch second it was
    created; a held lock with a large age and nothing in flight is a run that
    died holding it."""
    try:
        with open(os.path.join(LOCK, 'ts')) as f:
            return int(time.time()) - int(f.read().strip())
    except Exception:
        return None


def recent_build_activity(minutes=20):
    """True if the worker's live log or any build log moved within `minutes` --
    the same signal the SKILL's lock-steal reads to tell a live long build or
    land from a dead run. verdict() defers to it so a long build/land holding
    the lock is not misread as STALLED just because inflight() does not see it."""
    cutoff = time.time() - minutes * 60
    paths = [os.path.join(REPO, 'a', 'worker-live.log')]
    paths += glob.glob(os.path.join(REPO, 'a', 'build-logs', '*.log'))
    for p in paths:
        try:
            if os.path.getmtime(p) > cutoff:
                return True
        except OSError:
            pass
    return False


def verdict(rows, lock_held):
    undone = undelivered_wps()
    if not undone:
        return 'COMPLETE — every work package in the plan is delivered'
    failed = [n for n, s, _ in rows if s.startswith('FAILED')]
    if failed:
        return ('BUILD FAILED — %s broke; see a/build-logs/ '
                '(the worker retries it on its next run)' % ' '.join(failed))
    if any('BUILDING' in s for _, s, _ in rows):
        return 'BUILDING — a package is compiling; not stalled'
    if any(s.startswith('committed') or s.startswith('STARTED') for _, s, _ in rows):
        return 'work committed and awaiting the worker; not stalled'
    nb = next_buildable()
    if lock_held:
        # A real build shows an in-flight row and returned above; reaching here
        # with the lock held means nothing is in flight. Past a short startup
        # window that is an orphaned lock -- a run that died before STEP 4 and
        # never released it -- which jams every later run until the 3h steal.
        la = lock_age()
        if la is not None and la > 900 and recent_build_activity():
            return ('worker holds the lock; a build log moved in the last 20 min — a long '
                    'build or land is live, not stalled (bin/progress.py for what)')
        if la is not None and la > 900:
            return ('STALLED — the build-worker lock has been held %d min with nothing '
                    'in flight and no build log touched in 20 min; a run almost certainly '
                    'died holding it. Clear a/.build-worker.lock to recover now (the '
                    'worker auto-steals after 20 min idle, 3h cap).' % (la // 60))
        return 'worker holds the lock (starting or finishing %s); not stalled' % (nb or 'a package')
    if nb:
        age = last_commit_age()
        if age is not None and age > 900:      # >15 min = 3 missed heartbeats
            return ('STALLED — %s is buildable but there has been no commit in %d min; '
                    'the worker task may not be running' % (nb, age // 60))
        return ('READY — %s is next; the worker starts it on its next run '
                '(heartbeat every 5 min)' % nb)
    held = sorted(held_wps() & set(undone))
    tail = (' (held: %s; the rest wait on unmet dependencies)' % ' '.join(held)) if held \
           else ' (all waiting on unmet dependencies)'
    return 'BLOCKED — %d undelivered, none buildable right now%s' % (len(undone), tail)


def wp56_summary():
    """One line on WP-56's internal progress, computed the same way bin/progress.py
    does so the two can never disagree: wired = wire-<slice>.gen.c present,
    crossed = live-<slice>.sh present, denominator = census slices minus the two
    that are not wireable (unassigned residue, dl the runtime's own job)."""
    wiring = os.path.join(REPO, 'veneer', 'wiring')
    order_tsv = os.path.join(REPO, 'spike', 'demand-census', 'results', 'slice-order.tsv')
    try:
        slices = [l.split('\t')[0] for l in open(order_tsv, encoding='utf-8')
                  if l.strip() and not l.startswith('#')]
    except OSError:
        return None
    wireable = {s for s in slices if s not in ('unassigned', 'dl')}
    wired = {os.path.basename(p)[len('wire-'):-len('.gen.c')]
             for p in glob.glob(os.path.join(wiring, 'wire-*.gen.c'))} & wireable
    crossed = {os.path.basename(p)[len('live-'):-len('.sh')]
               for p in glob.glob(os.path.join(wiring, 't', 'live-*.sh'))} & wireable
    n = len(wireable)
    return ('WP-56: wiring %d/%d, live-crossing %d/%d, acceptance pending'
            '   (bin/progress.py for the slice/bucket/symbol tree)'
            % (len(wired), n, len(crossed), n))


def main():
    rows = inflight()
    lock_held = os.path.isdir(LOCK)
    print('march tip: %s' % git(['log', '--oneline', '-1']).strip())
    print('delivered: %d of %d   undelivered: %d   next buildable: %s   held: %s'
          % (delivered_count(), len(plan_wps()), len(undelivered_wps()),
             next_buildable() or '-', ' '.join(sorted(held_wps())) or '-'))
    print('last commits:')
    for l in git(['log', '-4', '--date=relative', '--pretty=format:  %h  %ad  %s']).splitlines():
        print(l)
    print('worker lock: %s' % ('held' if lock_held else 'free'))
    print('in flight:')
    if rows:
        for name, st, wp in rows:
            print('  %-22s %s  %s' % (name, st, wp))
    else:
        print('  (none)')
    print('verdict: %s' % verdict(rows, lock_held))
    s56 = wp56_summary()
    if s56:
        print(s56)
    if rows:
        print()
        print('logs (relative to project root):')
        for name, st, wp in rows:
            # The worker writes wp<NN>-<slug>.log; name is the <NN>-<slug>, so the
            # log carries a wp prefix. Without it this pointed at a file that never
            # exists and the section read "(not yet created)" for every in-flight
            # slice, log or no log.
            logname = 'wp%s.log' % name
            rel = 'a/build-logs/%s' % logname
            exists = os.path.isfile(os.path.join(REPO, 'a', 'build-logs', logname))
            print('  %-22s %s%s' % (name, rel, '' if exists else '   (not yet created)'))


if __name__ == '__main__':
    import sys
    if '--next' in sys.argv:
        print(next_buildable() or '')       # empty line = nothing buildable now
    else:
        main()
