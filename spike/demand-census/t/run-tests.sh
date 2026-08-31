#!/usr/bin/env bash
# Test census.py. Network-free; the ELF case reads the cross sysroot's libm
# and skips itself when the sysroot is not installed.
set -eu
cd "$(dirname "$0")"
exec python3 test-census.py
