#!/usr/bin/env bash
#
# Run the acceptance harness's unit tests -- host only, no network or image.
# The end-to-end verdict (accept.sh over a real package) is a separate, heavier
# run; these pin the pieces it is built from.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
bash "$here/classify.sh"
bash "$here/shape.sh"
echo "acceptance: unit tests passed"
