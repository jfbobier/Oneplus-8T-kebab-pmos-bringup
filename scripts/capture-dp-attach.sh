#!/bin/sh
# Capture a complete DisplayPort attach, with DRM DP logging armed.
#
# Run this ON THE DEVICE as root, with the monitor UNPLUGGED:
#
#   sudo scripts/capture-dp-attach.sh [TAIL_SECONDS]
#
# It arms the logging, tells you when to plug the cable in, waits for HPD,
# records for TAIL_SECONDS (default 25) so the link-training retries land
# in the log, then disarms and writes everything to a directory under /tmp.
#
# Why this exists: msm's DP link-training detail is behind drm_dbg_dp(),
# i.e. DRM debug category bit 8 (0x100), which is off by default. Without
# it the log shows that training failed but not the negotiated rate, the
# lane count, the voltage-swing sweep, or whether the driver ever tried to
# fall back to a lower rate or fewer lanes.
#
# Debug is always disarmed on exit, including on Ctrl-C.
set -u

TAIL="${1:-25}"
HPD_WAIT="${HPD_WAIT:-180}"
OUT="/tmp/kebab-dp-capture-$(date +%Y%m%d-%H%M%S)"
DRMDBG=/sys/module/drm/parameters/debug
DYNDBG=/sys/kernel/debug/dynamic_debug/control

[ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 1; }
mkdir -p "$OUT"

OLD_DBG=$(cat "$DRMDBG" 2>/dev/null || echo 0)

disarm() {
	echo "$OLD_DBG" > "$DRMDBG" 2>/dev/null || true
	if [ -w "$DYNDBG" ]; then
		for m in typec typec_displayport phy_qcom_qmp_combo \
		         qcom_pmic_typec typec_mux_fsa4480 tcpm; do
			echo "module $m -p" > "$DYNDBG" 2>/dev/null
		done
	fi
	echo "  (drm.debug restored to $OLD_DBG)"
}
trap 'disarm' EXIT INT TERM

# bit 1 DRIVER | bit 2 KMS | bit 8 DP
echo 0x106 > "$DRMDBG" 2>/dev/null \
	|| { echo "cannot write $DRMDBG" >&2; exit 1; }
if [ -w "$DYNDBG" ]; then
	for m in typec typec_displayport phy_qcom_qmp_combo qcom_pmic_typec \
	         typec_mux_fsa4480 tcpm; do
		echo "module $m +p" > "$DYNDBG" 2>/dev/null
	done
fi

MARK="kebab-dp-capture-start-$$"
echo "$MARK" > /dev/kmsg 2>/dev/null

echo "drm.debug armed (0x106 = DRIVER|KMS|DP)"
echo
echo "    >>> PLUG THE MONITOR IN NOW <<<"
echo

i=0
while [ "$i" -lt "$HPD_WAIT" ]; do
	s=$(cat /sys/class/drm/card*-DP-1/status 2>/dev/null | head -1)
	[ "$s" = "connected" ] && break
	i=$((i + 1))
	sleep 1
done

s=$(cat /sys/class/drm/card*-DP-1/status 2>/dev/null | head -1)
if [ "$s" = "connected" ]; then
	echo "HPD after ${i}s, recording for ${TAIL}s..."
else
	echo "no HPD within ${i}s; recording anyway in case the attach was not seen"
fi
sleep "$TAIL"

echo "collecting..."

# dmesg from our marker onwards
dmesg > "$OUT/dmesg-full.txt" 2>/dev/null
awk -v m="$MARK" 'f{print} $0 ~ m {f=1}' "$OUT/dmesg-full.txt" > "$OUT/dmesg-since-arm.txt"

# Type-C state.
#
# The alternate mode is its own device and a SIBLING of the partner --
# /sys/class/typec/port0-partner.0/ -- not a child of it. The DP altmode
# driver's "configuration" and "pin_assignment" attributes live there, and
# pin_assignment is what says whether we negotiated 4 DP lanes (assignment
# C or E) or 2 DP lanes plus USB3 (assignment D). An earlier version of
# this script looked under port0-partner/displayport/ and so reported
# nothing at all, which is easy to misread as "no altmode".
#
# Walk everything the class exposes rather than guessing paths.
{
	echo "## /sys/class/typec entries"
	ls -l /sys/class/typec/ 2>/dev/null
	echo
	for d in /sys/class/typec/*/; do
		echo "## $d"
		for f in "$d"*; do
			[ -f "$f" ] || continue
			case "$(basename "$f")" in uevent) continue ;; esac
			printf '  %-40s = %s\n' "$(basename "$f")" \
				"$(timeout 3 cat "$f" 2>/dev/null | head -1)"
		done
	done
} > "$OUT/typec.txt" 2>/dev/null

# DRM connector state
for c in /sys/class/drm/card*-*/; do
	n=$(basename "$c")
	{
		echo "## $n"
		for f in status enabled dpms modes; do
			echo "-- $f"; timeout 3 cat "$c/$f" 2>/dev/null
		done
	}
done > "$OUT/drm-connectors.txt" 2>/dev/null

for c in /sys/class/drm/card*-DP-1; do
	if [ -s "$c/edid" ]; then
		cp "$c/edid" "$OUT/dp-edid.bin" 2>/dev/null
		od -An -tx1 "$c/edid" > "$OUT/dp-edid.hex" 2>/dev/null
		command -v edid-decode >/dev/null 2>&1 \
			&& edid-decode "$c/edid" > "$OUT/dp-edid-decoded.txt" 2>&1
	fi
done

# msm debugfs.
#
# DO NOT glob DP-1/*. Reading dp_test_active, dp_test_data or dp_test_type
# oopses the kernel on external DisplayPort: msm_dp_debug_init() stores
# dp->msm_dp_display.connector, which is still NULL when debugfs is
# created, and all three show handlers dereference connector->status with
# no NULL check. An earlier version of this script globbed the directory
# and took three NULL-pointer oopses in a row. Whitelist instead.
#
# /sys/kernel/debug/dri/0/state takes the modeset locks and blocks if a
# commit is wedged, so everything here is timeout-guarded too.
for f in /sys/kernel/debug/dri/0/DP-1/dp_debug \
         /sys/kernel/debug/dri/0/DP-1/dp_link_status; do
	[ -f "$f" ] || continue
	{ echo "## $f"; timeout 3 cat "$f" 2>/dev/null; }
done > "$OUT/drm-debugfs.txt" 2>/dev/null
{
	echo "## skipped by design (they oops on external DP):"
	echo "##   dp_test_active, dp_test_data, dp_test_type"
} >> "$OUT/drm-debugfs.txt"
{ echo "## dri/0/state (may be empty if a commit is wedged)"
  timeout 5 cat /sys/kernel/debug/dri/0/state 2>/dev/null; } >> "$OUT/drm-debugfs.txt"

# The TCPM PD state machine does NOT log to dmesg -- it keeps its own ring
# buffer in debugfs, and reading it DRAINS it. Capture the whole thing in a
# single read; grepping it directly throws the rest away.
for d in /sys/kernel/debug/usb/tcpm-*; do
	[ -f "$d/log" ] || continue
	{ echo "## $d/log"; timeout 5 cat "$d/log" 2>/dev/null; }
done > "$OUT/tcpm-log.txt" 2>/dev/null

{
	for d in /sys/class/regulator/*/; do
		n=$(cat "$d/name" 2>/dev/null)
		case "$n" in otg-vbus|vreg_bob|*vbus*)
			echo "$n: state=$(cat "$d/state" 2>/dev/null) users=$(cat "$d/num_users" 2>/dev/null)" ;;
		esac
	done
} > "$OUT/regulators.txt" 2>/dev/null

disarm
trap - EXIT INT TERM

echo
echo "=============== SUMMARY ==============="
echo "--- negotiated link parameters (rate in 10kHz units, lanes) ---"
grep -aoE "rate=[0-9]+, num_lanes=[0-9]+, pixel_rate=[0-9]+" "$OUT/dmesg-since-arm.txt" | sort -u
echo "--- link training outcome ---"
grep -aE "link training|max v_level|lane_down_shift|rate_down_shift|Failed link training|link_ready|AUX" \
	"$OUT/dmesg-since-arm.txt" | head -25
echo "--- DP altmode: svid, pin assignment, orientation ---"
grep -aE "svid|pin_assignment|configuration|active|orientation|^## /sys" "$OUT/typec.txt" \
	| grep -avE "supported_accessory|port_type|preferred_role"
echo "--- altmode / mux / pin-assignment activity in the log ---"
grep -aiE "altmode|svid|pin_assign|typec_mux|dp_altmode|orientation|TYPEC_DP_STATE|set_mode" \
	"$OUT/dmesg-since-arm.txt" | grep -avE "Writing Reg:" | head -20
echo "--- PD / alternate-mode negotiation (TCPM) ---"
grep -aE "DISCOVER_IDENTITY|DISCOVER_SVIDS|DISCOVER_MODES|SVID |Alternate mode|Enter Mode|state change SNK_(READY|STARTUP)" \
	"$OUT/tcpm-log.txt" 2>/dev/null | tail -18
echo "--- DPU fallout ---"
echo "  frame-done timeouts: $(grep -ac 'frame done timeout' "$OUT/dmesg-since-arm.txt")"
echo "  vblank timeouts:     $(grep -ac 'vblank timeout' "$OUT/dmesg-since-arm.txt")"
echo
echo "Full capture in $OUT"
echo "Collect it with:  tar czf - -C /tmp $(basename "$OUT")"
