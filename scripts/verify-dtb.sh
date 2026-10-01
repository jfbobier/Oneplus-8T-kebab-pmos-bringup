#!/bin/sh
# Compare the compiled devicetree against a reference DTB, ignoring
# phandle renumbering.
#
#   scripts/verify-dtb.sh REFERENCE.dtb CANDIDATE.dtb DTC
#
# DTC is a dtc binary that can decompile (scripts/dtc/dtc from any built
# kernel tree will do). Used to prove that consolidating ~45 devicetree
# patches into one file changed nothing but the handful of things
# docs/REGRESSIONS.md says it changed.
set -eu
[ $# -eq 3 ] || { sed -n '2,12p' "$0"; exit 1; }
REF="$1"; CAND="$2"; DTC="$3"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
"$DTC" -I dtb -O dts -s -o "$tmp/ref.dts" "$REF" 2>/dev/null
"$DTC" -I dtb -O dts -s -o "$tmp/cand.dts" "$CAND" 2>/dev/null
exec python3 "$ROOT/scripts/dtb-structural-diff.py" "$tmp/ref.dts" "$tmp/cand.dts"
