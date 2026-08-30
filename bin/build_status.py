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
import os, re, time, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # the worktree root
PLAN = os.path.join(ROOT, 'doc', 'IMPLEMENTATION-PLAN.md')


def git(args, cwd=ROOT):
    try:
        return subprocess.check_output(['git', '-C', cwd] + args,
                                       stderr=subprocess.DEVNULL).decode()
    except Exception:
        return ''


def recent_files(path, secs=360):
    """Count files under path modified within the last `secs` seconds, skipping .git."""
    cutoff, n = time.time() - secs, 0
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
        r = recent_files(wt)
        if r > 0:
            st = 'BUILDING (%s files/6m, %s commit(s))' % (r, ahead)
        elif scaffold_only:
            st = 'STARTED (scaffold only, not built)'
        elif int(ahead) > 0:
            st = 'committed %s, idle (check: done or stalled)' % ahead
        else:
            st = 'idle'
        m = re.match(r'wp(\d+)', name)
        out.append((name, st, 'WP-%s' % m.group(1) if m else ''))
    return out


def delivered_wps():
    """The set of WP ids the plan marks Delivered or Partial (for check marks)."""
    try:
        text = open(PLAN, encoding='utf-8', errors='replace').read()
    except Exception:
        return set()
    out = set()
    parts = re.split(r'(?m)^### (WP-\d+)\b', text)
    for i in range(1, len(parts), 2):
        if re.search(r'(?m)^(Delivered|Partial)\b', parts[i + 1]):
            out.add(parts[i])
    return out


def delivered_count():
    return len(delivered_wps())


def verdict(rows, lock_held):
    if any('BUILDING' in s for _, s, _ in rows):
        return 'BUILDING — a package is compiling; not stalled'
    if any(s.startswith('committed') or s.startswith('STARTED') for _, s, _ in rows):
        return 'work committed and awaiting the worker; not stalled'
    if lock_held:
        return 'worker holds the lock (starting or finishing a package); not stalled'
    return 'STALLED — nothing building, nothing awaiting merge, lock free'


def main():
    rows = inflight()
    lock_held = os.path.isdir(os.path.join(ROOT, 'a', '.build-worker.lock'))
    print('march tip: %s' % git(['log', '--oneline', '-1']).strip())
    print('delivered markers: %d' % delivered_count())
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


if __name__ == '__main__':
    main()
