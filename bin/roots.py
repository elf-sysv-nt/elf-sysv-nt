"""The three roots, for the Python side. See bin/roots.sh for the reasoning.

Import it and read the module constants, or call expand() on a registry field
that was written with a root variable in it:

    from roots import ELFSYSVNT_EL8, expand
    if not os.path.exists(expand(needs)): ...

expand substitutes exactly the three names this module owns. It is not
os.path.expandvars, which would silently blank out any other $NAME it met and
turn a typo into an existence test against a truncated path.
"""
import os
import subprocess

ROOT = os.environ.get(
    "ELFSYSVNT_ROOT",
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PREFIX = os.environ.get("ELFSYSVNT_PREFIX", "/c/-/x-elfsysvnt")
EL8 = os.environ.get("ELFSYSVNT_EL8", "/c/-/el8")


def _main_checkout():
    """The checkout that owns .git: ROOT itself, or the one a session
    worktree was cut from. Asked of git, never read off the path; the old
    split on the annex path handed back the worktree's own path whenever
    the separator was missing, silently."""
    try:
        common = subprocess.check_output(
            ["git", "-C", ROOT, "rev-parse", "--git-common-dir"],
            stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return ROOT
    if not common:
        return ROOT
    # Relative (".git") from the main checkout's top level, absolute elsewhere.
    return os.path.dirname(os.path.abspath(os.path.join(ROOT, common)))


MAIN = os.environ.get("ELFSYSVNT_MAIN") or _main_checkout()
WT_ROOT = os.environ.get("ELFSYSVNT_WT_ROOT") or os.path.join(
    os.path.dirname(MAIN), ".worktrees", os.path.basename(MAIN))

_NAMES = {
    "ELFSYSVNT_ROOT": ROOT,
    "ELFSYSVNT_PREFIX": PREFIX,
    "ELFSYSVNT_EL8": EL8,
}


def expand(s):
    """Replace $NAME and ${NAME} for the three roots, and nothing else."""
    for name, value in _NAMES.items():
        s = s.replace("${%s}" % name, value).replace("$" + name, value)
    return s
