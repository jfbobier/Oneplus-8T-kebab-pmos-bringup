#!/bin/sh
# Install the refactored aports into a pmaports checkout.
#
#   scripts/install-to-pmaports.sh [PMAPORTS]           dry run
#   scripts/install-to-pmaports.sh [PMAPORTS] --force   actually do it
#
# PMAPORTS defaults to the "aports" path in ~/.config/pmbootstrap_v3.cfg.
#
# This replaces the contents of the kernel and device packages, so it lists
# what it would delete first and does nothing until you pass --force.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

PM=""
FORCE=0
for a in "$@"; do
	case "$a" in
	--force) FORCE=1 ;;
	*) PM="$a" ;;
	esac
done

cfgget() {
	for cfg in "$HOME/.config/pmbootstrap_v3.cfg" "$HOME/.config/pmbootstrap.cfg"; do
		[ -f "$cfg" ] || continue
		v=$(sed -n "s/^$1 *= *//p" "$cfg" | head -1)
		[ -n "$v" ] && { echo "$v"; return; }
	done
}

if [ -z "$PM" ]; then
	# "aports" is optional in pmbootstrap.cfg; when it is absent the
	# checkout lives under the work directory, which itself defaults to
	# ~/.local/var/pmbootstrap.
	PM=$(cfgget aports)
	if [ -z "$PM" ]; then
		WORK=$(cfgget work)
		PM="${WORK:-$HOME/.local/var/pmbootstrap}/cache_git/pmaports"
	fi
fi
[ -n "$PM" ] && [ -d "$PM" ] || { echo "pmaports checkout not found at '$PM'; pass it as an argument" >&2; exit 1; }

KDST="$PM/device/testing/linux-postmarketos-qcom-sm8250"
DDST="$PM/device/testing/device-oneplus-kebab"
[ -d "$KDST" ] || { echo "no $KDST" >&2; exit 1; }
[ -d "$DDST" ] || { echo "no $DDST" >&2; exit 1; }

stale=$(find "$KDST" -maxdepth 1 -name '*.patch' -printf '%P\n' | sort)
npatch=$(find "$ROOT/kernel" -maxdepth 1 -name '0*.patch' | wc -l)

# Say what is actually about to happen: a plan in dry-run mode, a report of
# work being done with --force. Printing "would ..." in both modes made a
# successful run look like it had changed nothing.
if [ "$FORCE" = "1" ]; then rm_v="removing"; cp_v="copying "; else rm_v="would remove"; cp_v="would copy  "; fi

echo "pmaports:      $PM"
echo
echo "kernel aport   $KDST"
echo "  $rm_v $(echo "$stale" | grep -c . || true) existing *.patch file(s)"
echo "$stale" | sed 's/^/    - /'
echo "  $cp_v APKBUILD, config, sm8250-oneplus-kebab.dts, $npatch patches"
echo
echo "device aport   $DDST"
echo "  $cp_v $(find "$ROOT/device/oneplus-kebab" -maxdepth 1 -type f | wc -l) file(s)"
echo
echo "extra packages under $PM/temp/"
echo "  kebab-modem-tools, tqftpserv-sdx55"
echo

if [ "$FORCE" != "1" ]; then
	echo "Dry run. Re-run with --force to apply."
	exit 0
fi

find "$KDST" -maxdepth 1 -name '*.patch' -delete
# Artifacts a previous abuild left in the aport directory. abuild warns
# about anything in there that is not in $source ("... is not in
# $source/$install/$triggers"), and a stale .dtb from an earlier build is
# the usual culprit.
find "$KDST" "$DDST" -maxdepth 1 -name '*.dtb' -delete
cp "$ROOT"/kernel/APKBUILD \
   "$ROOT"/kernel/config-postmarketos-qcom-sm8250.aarch64 \
   "$ROOT"/kernel/sm8250-oneplus-kebab.dts \
   "$ROOT"/kernel/0*.patch \
   "$KDST/"

cp "$ROOT"/device/oneplus-kebab/* "$DDST/"

mkdir -p "$PM/temp"
cp -a "$ROOT"/packages/kebab-modem-tools "$ROOT"/packages/tqftpserv-sdx55 "$PM/temp/"

echo "Done. Next:"
echo "  pmbootstrap build linux-postmarketos-qcom-sm8250"
echo "  pmbootstrap build device-oneplus-kebab"
echo "  pmbootstrap build kebab-modem-tools tqftpserv-sdx55   # modem only"
