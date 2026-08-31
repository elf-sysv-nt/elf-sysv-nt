#!/bin/sh
# Candidate runner for exercising diff-slice.sh end to end on el8: run the
# candidate binary on the pinned image, where the real ld.so and libc
# supply the behaviour. This judges the harness and the reference, not the
# veneer; the veneer run swaps in the elfsysv runtime as the runner.
# Usage: el8-run.sh BINARY   (image from LINUX_REF_DISTRO, default rocky8)
set -e
distro=${LINUX_REF_DISTRO:-rocky8}
wbin=$(wsl.exe -d "$distro" -- wslpath "$(cygpath -m "$1")" | tr -d '\r')
wsl.exe -d "$distro" -- bash -c "\"$wbin\"" 2>/dev/null | tr -d '\r'
