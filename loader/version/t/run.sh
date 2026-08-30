#!/usr/bin/env bash
# WP-36 certification: build the version matcher with the host compiler and run
# its unit test over version tables laid out the way an object carries them --
# the binding rule (@GLIBC_2.14 over GLIBC_2.2.5, default over hidden, a node's
# predecessor) and the load-refusal rule (present satisfied, weak tolerated,
# non-weak absent refused with ld.so's message).
set -eu
here=$(cd "$(dirname "$0")" && pwd)      # loader/version/t
loader=$(cd "$here/../.." && pwd)        # loader
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT

gcc -O1 -g -Wall -Wextra -I"$loader" \
    -o "$work/vtest" "$loader/version/elf_version.c" "$here/version_test.c"
"$work/vtest"
