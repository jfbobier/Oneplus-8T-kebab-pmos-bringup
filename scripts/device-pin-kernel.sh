#!/bin/sh
# Make apk on the device agree with the kernel we actually built.
#
#   scripts/device-pin-kernel.sh [--dry-run]
#
# Run on the BUILDER. Needs the device reachable over ssh.
#
# THE PROBLEM
#
# The device's apk database says the kernel is the postmarketOS repo build
# (7.2.0-r0) while the running kernel is ours. That is not cosmetic: apk
# owns 584 files under usr/lib/modules/7.2.0/ plus /boot/vmlinuz and the
# DTBs. So `apk fix`, `apk upgrade`, or any operation that pulls the kernel
# as a dependency will lay the repo kernel's modules over our running
# kernel. They then fail BTF validation and Wi-Fi, audio, camera and the
# modem all vanish until the modules are re-synced -- the exact failure
# seen on the first boot after flashing.
#
# Pinning alone does not fix it. The device already had
#   linux-postmarketos-qcom-sm8250=7.2.0-r0
# in /etc/apk/world, which stops version *changes* but still lets apk
# restore r0's files, because as far as apk is concerned r0 is what is
# installed.
#
# THE FIX
#
#   1. trust pmbootstrap's local signing key, so our packages verify
#   2. install our own .apk, so the database is truthful, every file is
#      accounted for and root-owned
#   3. pin that exact version in /etc/apk/world
#
# Step 2 also replaces the hand-rolled module tarball sync: apk puts the
# modules in place itself, with correct ownership.
#
# SAFETY
#
# Nothing here touches the boot partition. /boot on this device is
# loop0p1 inside the rootfs image, and the device boots from the flashed
# boot_a partition, so the /boot/boot.img that boot-deploy regenerates is
# a staging file only. The running kernel is not affected by this script;
# it only makes the next apk operation harmless.
set -eu

PHONE="${PHONE:-172.16.42.1}"
PHONE_USER="${PHONE_USER:-$USER}"
PKG=linux-postmarketos-qcom-sm8250
DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

say() { echo "==> $*"; }
dev() { ssh "$PHONE_USER@$PHONE" "$@"; }

WORK=""
for cfg in "$HOME/.config/pmbootstrap_v3.cfg" "$HOME/.config/pmbootstrap.cfg"; do
	[ -f "$cfg" ] || continue
	WORK=$(sed -n 's/^work *= *//p' "$cfg" | head -1)
	[ -n "$WORK" ] && break
done
WORK="${WORK:-$HOME/.local/var/pmbootstrap}"

# newest locally built kernel apk
APK=$(ls -1t "$WORK"/packages/edge/aarch64/$PKG-*.apk 2>/dev/null | head -1)
[ -n "$APK" ] || { echo "no locally built $PKG apk under $WORK/packages" >&2; exit 1; }
VER=$(basename "$APK" | sed -E "s/^$PKG-(.*)\.apk$/\1/")

KEY=$(ls -1 "$WORK"/config_apk_keys/pmos@local-*.rsa.pub 2>/dev/null | head -1)
[ -n "$KEY" ] || { echo "no pmos@local signing key under $WORK/config_apk_keys" >&2; exit 1; }

say "local package : $(basename "$APK")"
say "version       : $VER"
say "signing key   : $(basename "$KEY")"
say "device        : $PHONE_USER@$PHONE"
echo

say "device state before"
dev "uname -v; sudo -n apk list -I 2>/dev/null | grep '^$PKG'; grep '^$PKG' /etc/apk/world || true"
echo

if [ "$DRY" = 1 ]; then
	say "dry run; would trust the key, install $VER and pin it. Nothing done."
	exit 0
fi

say "trusting the local signing key"
scp -q "$KEY" "$PHONE_USER@$PHONE:/tmp/$(basename "$KEY")"
dev "sudo -n install -Dm644 /tmp/$(basename "$KEY") /etc/apk/keys/$(basename "$KEY") && rm -f /tmp/$(basename "$KEY")"

say "copying the package ($(du -h "$APK" | cut -f1))"
scp -q "$APK" "$PHONE_USER@$PHONE:/tmp/$(basename "$APK")"

# Drop any existing constraint first, or apk refuses to move off the pinned
# version. Note apk-tools 3 writes its own constraint syntax when a package
# is installed from a local file:
#
#   linux-postmarketos-qcom-sm8250><Q1tEiOkgccGKFHubXAgdSHKDPOdgU=
#
# that is a content hash, not a version. A sed matching only "pkg=" leaves
# it behind and the entries accumulate, so match every operator apk can
# emit.
WORLD_RE="^$PKG([=<>!~]|\$)"

say "installing it (this also regenerates /boot/initramfs and /boot/boot.img)"
dev "sudo -n sh -s" <<EOSH
set -e
sed -i -E "/$WORLD_RE/d" /etc/apk/world
apk add "/tmp/$(basename "$APK")"
rm -f "/tmp/$(basename "$APK")"
EOSH

# apk has now re-added its own content constraint. Add the readable version
# pin next to it: both are satisfied by the installed package, and the
# version pin is the one that unambiguously blocks a move to a repo build.
say "pinning $PKG=$VER in /etc/apk/world"
dev "sudo -n sh -s" <<EOSH
set -e
sed -i -E "/^$PKG=/d" /etc/apk/world
echo "$PKG=$VER" >> /etc/apk/world
sort -u -o /etc/apk/world /etc/apk/world
EOSH

echo
say "device state after"
dev "sudo -n apk list -I 2>/dev/null | grep '^$PKG'; grep '^$PKG' /etc/apk/world"
echo
say "proving apk no longer wants to touch the kernel"
echo "--- apk upgrade --simulate ---"
dev "sudo -n apk upgrade --simulate 2>&1 | grep -iE '$PKG|^OK|nothing' | head -5 || true"
echo "--- apk fix --simulate ---"
dev "sudo -n apk fix --simulate 2>&1 | grep -iE '$PKG|^OK|nothing' | head -5 || true"
echo
say "done. Run scripts/check-kernel-drift.sh on the device to confirm."
