#!/bin/sh
# Show what upstream changed in the board devicetree underneath our
# whole-file copy.
#
#   scripts/check-dts-drift.sh [KERNEL_SOURCE_DIR]
#
# The board dts is maintained as a complete file rather than as a patch
# stack, which means a kernel bump cannot produce a merge conflict -- and
# also that an upstream improvement would be silently dropped. Run this
# after every kernel bump.
#
# With no argument it fetches the tag named in kernel/APKBUILD from the
# pmbootstrap distfiles cache, if present.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OURS="$ROOT/kernel/sm8250-oneplus-kebab.dts"
REL="arch/arm64/boot/dts/qcom/sm8250-oneplus-kebab.dts"

SRC="${1:-}"
if [ -z "$SRC" ]; then
	ver=$(sed -n 's/^pkgver=//p' "$ROOT/kernel/APKBUILD")
	# Resolve the pmbootstrap work directory the same way kebab-build.sh
	# does. Hardcoding $HOME/pmbootstrap worked on the workstation and
	# silently failed on the Arch build host, where work is
	# $HOME/.local/var/pmbootstrap -- i.e. exactly on the machine where a
	# kernel bump is actually done.
	WORK=""
	for cfg in "$HOME/.config/pmbootstrap_v3.cfg" "$HOME/.config/pmbootstrap.cfg"; do
		[ -f "$cfg" ] || continue
		WORK=$(sed -n 's/^work *= *//p' "$cfg" | head -1)
		[ -n "$WORK" ] && break
	done
	tar=""
	for d in "$WORK/cache_distfiles" "$HOME/.local/var/pmbootstrap/cache_distfiles" \
	         "$HOME/pmbootstrap/cache_distfiles"; do
		[ -n "$d" ] || continue
		if [ -f "$d/linux-postmarketos-qcom-sm8250-sm8250-$ver.tar.gz" ]; then
			tar="$d/linux-postmarketos-qcom-sm8250-sm8250-$ver.tar.gz"
			break
		fi
	done
	[ -n "$tar" ] || { echo "no cached tarball for $ver; pass a kernel source directory, or run 'pmbootstrap build linux-postmarketos-qcom-sm8250' once to fetch it" >&2; exit 1; }
	tmp=$(mktemp -d)
	trap 'rm -rf "$tmp"' EXIT
	tar xzf "$tar" -C "$tmp" "linux-sm8250-$ver/$REL"
	SRC="$tmp/linux-sm8250-$ver"
fi

[ -f "$SRC/$REL" ] || { echo "no $SRC/$REL" >&2; exit 1; }

echo "upstream: $SRC/$REL"
echo "ours:     $OURS"
echo
diff -u "$SRC/$REL" "$OURS" || true
