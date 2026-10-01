#!/bin/sh
# Install the kebab userspace rules and configuration on a running device.
#
# Run this ON THE DEVICE, from a directory holding the files from
# device/oneplus-kebab/:
#
#   scripts/install-userspace-on-device.sh [SRCDIR]
#
# This is the "no packages yet" path: it puts the same files in the same
# places the device-oneplus-kebab package would, so a device can be brought
# up to the refactored userspace before the apks exist.
#
# Deliberately additive and conservative:
#   * everything lands in /usr/lib/..., never /etc/..., so any hand-made
#     override already in /etc keeps winning;
#   * nothing is enabled or disabled -- unit enablement is left exactly as
#     it was;
#   * every file it replaces is backed up next to itself with a .pre-refactor
#     suffix the first time only.
set -eu

SRC="${1:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)}"
[ -f "$SRC/kebab-bluetooth-addr" ] || { echo "no kebab-bluetooth-addr in $SRC" >&2; exit 1; }

say() { echo "  $*"; }

inst() {
	mode="$1"; src="$2"; dst="$3"
	if [ -e "$dst" ]; then
		if cmp -s "$src" "$dst"; then
			say "unchanged  $dst"
			return 0
		fi
		[ -e "$dst.pre-refactor" ] || cp -a "$dst" "$dst.pre-refactor"
		say "replaced   $dst  (backup: $dst.pre-refactor)"
	else
		say "new        $dst"
	fi
	install -Dm"$mode" "$src" "$dst"
}

echo "== Bluetooth: per-device address (replaces the devicetree constant)"
inst 0755 "$SRC/kebab-bluetooth-addr"            /usr/bin/kebab-bluetooth-addr
inst 0644 "$SRC/kebab-bluetooth-addr@.service"   /usr/lib/systemd/system/kebab-bluetooth-addr@.service
inst 0644 "$SRC/90-kebab-bluetooth-addr.rules"   /usr/lib/udev/rules.d/90-kebab-bluetooth-addr.rules

echo "== SDX55 modem: ModemManager rules"
inst 0644 "$SRC/77-mm-ignore-sdx55-efs.rules"    /usr/lib/udev/rules.d/77-mm-ignore-sdx55-efs.rules
inst 0644 "$SRC/77-mm-sdx55-fusion.rules"        /usr/lib/udev/rules.d/77-mm-sdx55-fusion.rules

echo "== SDX55 modem: MHI debug logging policy"
# Documents why the mhi module runs with every dev_dbg site on, what it
# costs in dmesg, and how to turn it off. See NOISE-3 in
# docs/REGRESSIONS.md. A hand-placed /etc/modprobe.d/mhi.conf from the
# original bring-up says the same thing and takes precedence; removing it
# leaves this packaged, commented copy in charge.
inst 0644 "$SRC/90-kebab-mhi-debug.conf"         /usr/lib/modprobe.d/90-kebab-mhi-debug.conf

echo "== SDX55 modem: units and helpers"
inst 0755 "$SRC/kebab-uim-provision"             /usr/bin/kebab-uim-provision
inst 0644 "$SRC/kebab-uim-provision.service"     /usr/lib/systemd/system/kebab-uim-provision.service
inst 0644 "$SRC/kebab-pcie2-no-runtime-pm.service" \
	/usr/lib/systemd/system/kebab-pcie2-no-runtime-pm.service

echo "== Audio: ALSA UCM profile"
inst 0644 "$SRC/ucm-OnePlus-8T.conf" "/usr/share/alsa/ucm2/conf.d/sm8250/OnePlus 8T.conf"
inst 0644 "$SRC/ucm-HiFi.conf" \
	/usr/share/alsa/ucm2/Qualcomm/sm8250/OnePlus8T/HiFi.conf

echo "== Reloading"
udevadm control --reload-rules && say "udev rules reloaded"
systemctl daemon-reload && say "systemd reloaded"

echo
echo "Unit enablement was NOT changed. Current state:"
systemctl list-unit-files 2>/dev/null \
	| grep -E 'kebab|mhi-efs|tqftp|pm-service' || true
