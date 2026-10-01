#!/bin/sh
# Build, flash and -- crucially -- resync modules for kebab.
#
#   scripts/kebab-build.sh                 install aports, build, flash, deploy
#   scripts/kebab-build.sh --no-install    skip copying aports into pmaports
#   scripts/kebab-build.sh --no-flash      build and deploy, no fastboot step
#   scripts/kebab-build.sh --deploy-only   only deploy to the device
#   scripts/kebab-build.sh --modules-tar   deploy by tarball instead of apk
#   scripts/kebab-build.sh --modem         also build the SDX55 userspace
#
# Environment: PHONE (default 172.16.42.1), PHONE_USER (default $USER).
#
# WHY THIS EXISTS AND WHY YOU SHOULD NOT USE BARE flash_kernel:
# `pmbootstrap flasher flash_kernel` writes boot.img only. Everything built
# as a module keeps running the previous build until /usr/lib/modules is
# updated. During bring-up that caused four separate "regressions" that were
# really just stale modules, including Wi-Fi disappearing after a boot that
# had worked.
#
# HOW THE MODULES GET THERE, AND WHY IT IS AN APK
# The default is to install the locally built .apk on the device
# (device-pin-kernel.sh). That does three things a tarball cannot:
#
#   * apk's database ends up telling the truth. Otherwise it still believes
#     the postmarketOS repo kernel is installed, and it owns 584 files under
#     usr/lib/modules/ plus /boot/vmlinuz -- so `apk fix`, `apk upgrade` or
#     any dependency pull will lay the repo kernel's modules over our
#     running kernel. The modules then fail BTF validation and Wi-Fi, audio,
#     camera and the modem all disappear on the next boot.
#   * the files land root-owned, which a tar of the build chroot does not
#     manage.
#   * /boot/vmlinuz and the regenerated /boot/boot.img match the kernel that
#     was actually flashed, instead of staying on the repo build.
#
# --modules-tar keeps the old tarball path for when apk on the device is
# broken or offline. It still verifies both trees hash the same.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KPKG=linux-postmarketos-qcom-sm8250
DPKG=device-oneplus-kebab
PHONE="${PHONE:-172.16.42.1}"
PHONE_USER="${PHONE_USER:-$USER}"

do_install=1; do_flash=1; do_build=1; do_modem=0; deploy=apk
for a in "$@"; do
	case "$a" in
	--no-install)   do_install=0 ;;
	--no-flash)     do_flash=0 ;;
	--deploy-only)  do_install=0; do_build=0; do_flash=0 ;;
	--modules-only) do_install=0; do_build=0; do_flash=0 ;;   # old name
	--modules-tar)  deploy=tar ;;
	--modem)        do_modem=1 ;;
	*) echo "unknown option: $a" >&2; exit 1 ;;
	esac
done

WORK=""
for cfg in "$HOME/.config/pmbootstrap_v3.cfg" "$HOME/.config/pmbootstrap.cfg"; do
	[ -f "$cfg" ] || continue
	WORK=$(sed -n 's/^work *= *//p' "$cfg" | head -1)
	[ -n "$WORK" ] && break
done
WORK="${WORK:-$HOME/.local/var/pmbootstrap}"
CHROOT="$WORK/chroot_rootfs_oneplus-kebab"

if [ "$do_install" = 1 ]; then
	echo "==> installing aports"
	"$ROOT/scripts/install-to-pmaports.sh" --force
fi

if [ "$do_build" = 1 ]; then
	echo "==> checksums"
	pmbootstrap checksum "$KPKG"
	pmbootstrap checksum "$DPKG"
	echo "==> building kernel"
	pmbootstrap build "$KPKG" --force
	echo "==> building device package"
	pmbootstrap build "$DPKG" --force
	if [ "$do_modem" = 1 ]; then
		echo "==> building SDX55 userspace"
		pmbootstrap checksum kebab-modem-tools
		pmbootstrap checksum tqftpserv-sdx55
		pmbootstrap build kebab-modem-tools --force
		pmbootstrap build tqftpserv-sdx55 --force
	fi
fi

if [ "$do_flash" = 1 ]; then
	echo "==> flashing (phone must be in fastboot)"
	pmbootstrap flasher flash_kernel
	printf '==> boot the phone back into postmarketOS, then press enter: '
	read -r _
fi

if [ "$deploy" = apk ]; then
	echo "==> deploying the kernel to the device as an apk"
	"$ROOT/scripts/device-pin-kernel.sh"
	echo "==> checking for drift"
	scp -q "$ROOT/scripts/check-kernel-drift.sh" "$PHONE_USER@$PHONE:/tmp/"
	ssh "$PHONE_USER@$PHONE" "sudo -n sh /tmp/check-kernel-drift.sh" || {
		echo "drift check failed -- do not trust test results until resolved" >&2
		exit 1
	}
	echo "==> reboot the phone to pick everything up"
	exit 0
fi

KVER=$(ls "$CHROOT/lib/modules" | head -1)
[ -n "$KVER" ] || { echo "no module tree in $CHROOT/lib/modules" >&2; exit 1; }
echo "==> syncing modules for $KVER"

T=$(mktemp /tmp/kebab-mods-XXXXXX.tar.gz)
# `sudo tar czf "$T"` fails when $T came from mktemp as your own user: tar's
# compression child cannot open it and you get a 45-byte archive.
sudo tar cf - -C "$CHROOT" "lib/modules/$KVER" | gzip > "$T"
sz=$(stat -c %s "$T")
[ "$sz" -gt 1000000 ] || { echo "module tarball is only $sz bytes, aborting" >&2; rm -f "$T"; exit 1; }

scp "$T" "$PHONE_USER@$PHONE:/tmp/kebab-mods.tar.gz"

# REPLACE the tree, do not overlay it. `tar x` only adds and overwrites, so a
# module that the new kernel no longer builds stays on disk and can still be
# autoloaded. That is not hypothetical: dropping the second TFA9874 amplifier
# driver left snd-soc-tfa9872.ko behind, and it matches the same devicetree
# compatible as the driver that replaced it.
#
# The old tree is moved aside rather than deleted, and only removed once the
# new one is in place, so a failed transfer is recoverable.
ssh "$PHONE_USER@$PHONE" "sudo sh -s" <<EOSH
set -e
rm -rf "/lib/modules/$KVER.replaced"
[ -d "/lib/modules/$KVER" ] && mv "/lib/modules/$KVER" "/lib/modules/$KVER.replaced"
if ! tar xzf /tmp/kebab-mods.tar.gz -C /; then
	echo "extract failed, restoring the previous module tree" >&2
	rm -rf "/lib/modules/$KVER"
	mv "/lib/modules/$KVER.replaced" "/lib/modules/$KVER"
	exit 1
fi
depmod -a "$KVER"
rm -rf "/lib/modules/$KVER.replaced"
rm -f /tmp/kebab-mods.tar.gz
EOSH
rm -f "$T"

echo "==> verifying the module trees match"
# Hash the module tree the same way on both sides: relative path plus
# content, sorted, then one hash over the lot.
tree_sum='cd "$1" && find . -name "*.ko*" -type f | LC_ALL=C sort | xargs -r sha256sum | sha256sum | cut -c1-16'
local_sum=$(sudo sh -c "$tree_sum" _ "$CHROOT/lib/modules/$KVER")
remote_sum=$(ssh "$PHONE_USER@$PHONE" "sudo sh -c '$tree_sum' _ /lib/modules/$KVER")
if [ "$local_sum" != "$remote_sum" ]; then
	echo "MODULE TREES DIFFER: builder $local_sum, phone $remote_sum" >&2
	echo "Do not trust any test result until this is resolved." >&2
	exit 1
fi
echo "==> module trees match ($local_sum)"
echo "==> reboot the phone to pick everything up"
