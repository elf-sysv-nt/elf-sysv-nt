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

ROOT = os.environ.get(
    "ELFSYSVNT_ROOT",
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PREFIX = os.environ.get("ELFSYSVNT_PREFIX", "/c/-/x-elfsysvnt")
EL8 = os.environ.get("ELFSYSVNT_EL8", "/c/-/el8")

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
