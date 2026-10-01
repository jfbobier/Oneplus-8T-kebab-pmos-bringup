#!/bin/sh
# Test whether the patch series still applies to a kernel tree, and say
# which patches need attention.
#
#   scripts/try-patches.sh KERNEL_SOURCE_DIR
#
# This is the one genuinely new step in a kernel bump. Everything else
# (install-to-pmaports.sh, kebab-build.sh) is unchanged from a normal
# rebuild; what a bump risks is that upstream moved the code a patch
# touches -- or fixed the bug a patch fixes.
#
# For each patch it reports one of:
#
#   APPLIES    nothing to do
#   FUZZ       applies with offsets; fine, but regenerate the patch
#   CONFLICT   needs a human
#   REDUNDANT  the reverse applies cleanly, i.e. the change is already in
#              the tree -- DROP the patch, do not force it in
#
# REDUNDANT is the outcome to hope for on 0004, 0005, 0007, 0012 and
# 0014-0018: those are generic fixes with no kebab specifics, so they are
# the ones upstream may have taken. Dropping one is a win, not a loss.
#
# Point this at a PRISTINE tree. Run against an already-patched tree it
# reports 17 REDUNDANT and one CONFLICT on 0008, because 0009 modifies the
# file 0008 adds, so 0008 can no longer be reversed on its own. That is a
# property of the self-test, not of the series.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC="${1:-}"
[ -n "$SRC" ] && [ -d "$SRC" ] || { echo "usage: $0 KERNEL_SOURCE_DIR" >&2; exit 1; }
[ -f "$SRC/Makefile" ] || { echo "$SRC does not look like a kernel tree" >&2; exit 1; }

cd "$SRC"
applies=0; fuzz=0; conflict=0; redundant=0

for p in "$ROOT"/kernel/0*.patch; do
	b=$(basename "$p")
	n=${b%%-*}
	if git apply --check --whitespace=nowarn "$p" 2>/dev/null; then
		verdict="APPLIES"; applies=$((applies+1))
	elif git apply --check --reverse --whitespace=nowarn "$p" 2>/dev/null; then
		verdict="REDUNDANT"; redundant=$((redundant+1))
	elif git apply --check -C1 --whitespace=nowarn "$p" 2>/dev/null; then
		verdict="FUZZ"; fuzz=$((fuzz+1))
	else
		verdict="CONFLICT"; conflict=$((conflict+1))
	fi

	# Patches that only add files are near-impossible to conflict on
	# content; if they fail it is the Kconfig/Makefile hunk.
	kind=modifies
	[ "$(grep -c '^new file mode' "$p")" -gt 0 ] && kind=adds

	printf '%-6s %-10s %-9s %s\n' "$n" "$verdict" "$kind" \
		"$(grep '^diff --git' "$p" | sed 's|^diff --git a/||; s| b/.*||' | tr '\n' ' ')"

	# Apply as we go so later patches see earlier ones (0009 edits the
	# file 0008 adds; 0012 and 0017 edit files 0011 and 0007 touched).
	[ "$verdict" = APPLIES ] && git apply --whitespace=nowarn "$p"
done

echo
echo "applies=$applies fuzz=$fuzz redundant=$redundant conflict=$conflict"
[ "$conflict" = 0 ] || { echo "resolve the CONFLICT patches before building" >&2; exit 1; }
