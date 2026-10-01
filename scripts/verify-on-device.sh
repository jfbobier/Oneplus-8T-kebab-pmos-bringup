#!/bin/sh
# Post-flash smoke test. Run this ON THE DEVICE.
#
#   scripts/verify-on-device.sh
#
# Walks the subsystems this refactor touched, in the order their failures
# cascade, and prints PASS/FAIL/SKIP per check. Read-only: it inspects
# state, loads nothing and changes nothing.
#
# A FAIL names the docs/REGRESSIONS.md item to read.
set -u

pass=0; fail=0; skip=0

ok()   { echo "  PASS  $1"; pass=$((pass+1)); }
no()   { echo "  FAIL  $1${2:+  -> $2}"; fail=$((fail+1)); }
na()   { echo "  SKIP  $1${2:+  ($2)}"; skip=$((skip+1)); }
head_() { echo; echo "== $1"; }

head_ "Kernel"
# The refactored series started at pkgrel 19 -> "#20" and climbs with each
# rebuild, so match anything from #20 up rather than pinning a number.
# "#19" and below is the pre-refactor bring-up kernel.
kv=$(uname -v)
rel=$(printf '%s' "$kv" | sed -n 's/^#\([0-9]\+\)-postmarketos-qcom-sm8250.*/\1/p')
if [ -z "$rel" ]; then
	na "unrecognised build version" "$kv"
elif [ "$rel" -ge 20 ]; then
	ok "running the refactored kernel (#$rel)"
else
	no "still on the pre-refactor kernel (#$rel)" "the flash did not take"
fi

n=$(lsmod 2>/dev/null | tail -n +2 | wc -l)
if [ "$n" -gt 30 ]; then
	ok "$n modules loaded"
else
	no "only $n modules loaded" "stale module tree; resync with kebab-build.sh --modules-only"
fi
if dmesg 2>/dev/null | grep -q "failed to validate module"; then
	no "modules rejected by BTF validation" "module tree does not match this kernel"
else
	ok "no BTF module-validation failures"
fi

head_ "Display and touch"
[ -n "$(ls /sys/class/drm/card*-DSI-1 2>/dev/null)" ] \
	&& ok "DSI connector present ($(cat /sys/class/drm/card*-DSI-1/status 2>/dev/null))" \
	|| no "no DSI connector" "panel/dispcc"
grep -qi "synaptics" /proc/bus/input/devices 2>/dev/null \
	&& ok "Synaptics touchscreen registered" \
	|| no "no Synaptics touchscreen input device"

head_ "Wi-Fi and Bluetooth"
ip link show 2>/dev/null | grep -q wlan \
	&& ok "wlan interface present" || no "no wlan interface" "ath11k / QCA6390 PMU"

if [ -d /sys/class/bluetooth/hci0 ]; then
	addr=$(btmgmt_bin=""; for c in /usr/bin/btmgmt /usr/sbin/btmgmt; do [ -x "$c" ] && btmgmt_bin="$c"; done
	       [ -n "$btmgmt_bin" ] && "$btmgmt_bin" --index 0 info 2>/dev/null \
	       | sed -n 's/^[[:space:]]*addr \([0-9A-Fa-f:]*\).*/\1/p' | head -1)
	case "$addr" in
		02:00:37:F1:24:55|02:00:37:f1:24:55)
			no "Bluetooth still on the old devicetree address ($addr)" "BT-1" ;;
		02:*)
			ok "Bluetooth has a per-device address ($addr)" ;;
		00:00:00:00:5A:AD|00:00:00:00:5a:ad|"")
			no "Bluetooth unconfigured (addr '${addr:-none}')" "BT-1" ;;
		*)
			na "Bluetooth address is $addr" "not locally administered" ;;
	esac
else
	no "no hci0" "QCA6390 bluetooth did not register"
fi

head_ "Audio"
aplay -l 2>/dev/null | grep -q "OnePlus 8T" \
	&& ok "sound card present" || no "no OnePlus 8T sound card"
if lsmod 2>/dev/null | grep -q "^snd_soc_tfa2"; then
	ok "snd-soc-tfa2 loaded (speaker amplifiers)"
else
	no "snd-soc-tfa2 not loaded" "AUDIO-1"
fi
if lsmod 2>/dev/null | grep -q "tfa9872"; then
	no "snd-soc-tfa9872 is loaded" "AUDIO-1: two drivers claim nxp,tfa9874"
else
	ok "snd-soc-tfa9872 absent, as intended"
fi
if find /lib/modules -name 'snd-soc-tfa9872*' 2>/dev/null | grep -q .; then
	no "stale snd-soc-tfa9872 module still on disk" "AUDIO-1: it can still autoload"
else
	ok "no stale tfa9872 module on disk"
fi
# AUDIO-3: both amplifiers must take their own TDM slot, and their DAPM
# widgets must not collide. Before r22 the driver clamped every instance to
# slot 0 and both components registered identically named widgets, so the
# second amp never left power-down.
amps=$(dmesg 2>/dev/null | sed -n 's/.*tfa2 \(15-00[0-9a-f]*\): tfa9874 rev .* ready (channel=\([0-9]\).*/\1 slot\2/p' | sort -u)
nslot=$(printf '%s\n' "$amps" | awk '{print $2}' | sort -u | grep -c . || true)
if [ -z "$amps" ]; then
	na "no tfa9874 probe lines in dmesg" "ring buffer wrapped? try journalctl -k -b"
elif [ "$(printf '%s\n' "$amps" | grep -c .)" -lt 2 ]; then
	no "only one tfa9874 amplifier probed" "AUDIO-3: expected 15-0034 and 15-0035"
elif [ "$nslot" -lt 2 ]; then
	no "both amplifiers on the same TDM slot ($amps)" "AUDIO-3: playback is mono"
else
	ok "amplifiers on separate TDM slots ($(printf '%s' "$amps" | tr '\n' ' '))"
fi
if dmesg 2>/dev/null | grep -q "tfa2 .*widget .* overwritten"; then
	no "tfa2 DAPM widget names collide" "AUDIO-3: add sound-name-prefix to both amp nodes"
else
	ok "no tfa2 DAPM widget collision"
fi

# NOISE-2: these three used to be dev_err on paths reached in normal
# operation. Patches 0016-0018 demoted them; if they are back, a patch was
# dropped from the series.
noisy=0
for pat in "isr: tx_sig" "Impedance detect ramp error" "Handover signaled, but it already happened"; do
	n=$(dmesg 2>/dev/null | grep -c "$pat" || true)
	[ "$n" -gt 0 ] && { no "still logging: $pat (x$n)" "NOISE-2: patch 0016/0017/0018 missing"; noisy=1; }
done
[ "$noisy" = 0 ] && ok "no demoted error-level messages in dmesg"

# PipeWire lives in the logged-in user's session, so running this script
# under sudo sees nothing. Reach into the session when we are root.
pw() {
	if [ "$(id -u)" = 0 ]; then
		u=$(loginctl list-users --no-legend 2>/dev/null | awk '$2!="root"{print $2; exit}')
		[ -n "$u" ] || return 1
		uid=$(id -u "$u" 2>/dev/null) || return 1
		su "$u" -c "XDG_RUNTIME_DIR=/run/user/$uid pactl $1" 2>/dev/null
	else
		pactl $1 2>/dev/null
	fi
}
if command -v pactl >/dev/null 2>&1; then
	pw "list sinks short" | grep -q "Speaker" \
		&& ok "PipeWire speaker sink" || na "no PipeWire speaker sink" "no active user session?"
	pw "list sources short" | grep -q "Mic" \
		&& ok "PipeWire mic source" || na "no PipeWire mic source" "no active user session?"
fi

head_ "Camera"
if [ -e /dev/media0 ] && command -v media-ctl >/dev/null 2>&1; then
	media-ctl -p -d /dev/media0 2>/dev/null | grep -q "imx471" \
		&& ok "IMX471 in the media graph" || no "IMX471 missing from the media graph" "CAMERA-1/2"
else
	na "no /dev/media0 or media-ctl" "camss"
fi
dmesg 2>/dev/null | grep -q "imx471.*rails up" \
	&& ok "IMX471 three-rail power sequence ran" || na "no 'rails up' line" "sensor may not have been opened yet"

head_ "USB-C, charging, DisplayPort"
if grep -q . /sys/class/regulator/*/name 2>/dev/null; then
	if grep -lx "otg-vbus" /sys/class/regulator/*/name >/dev/null 2>&1; then
		ok "otg-vbus regulator registered (MP2762A)"
	else
		no "no otg-vbus regulator" "DT-4: mps,mp2762a compatible vs driver match table"
	fi
fi
dmesg 2>/dev/null | grep -q "vbus vsafe5v fail" \
	&& no "'vbus vsafe5v fail' in dmesg" "USB-1: regulator ramp delay not taking effect" \
	|| ok "no vSafe5V timeout warnings"
dmesg 2>/dev/null | grep -qi "frame done timeout" \
	&& no "DPU frame-done timeouts" "DP-2" || ok "no DPU frame-done timeouts"
ls /sys/class/drm/card*-DP-1 >/dev/null 2>&1 \
	&& ok "DisplayPort connector present ($(cat /sys/class/drm/card*-DP-1/status 2>/dev/null))" \
	|| na "no DP connector" "mdss_dp"

head_ "External modem (SDX55)"
cat /sys/devices/platform/soc@0/1c10000.pcie/power/control 2>/dev/null | grep -qx on \
	&& ok "PCIe2 host controller pinned out of runtime suspend" \
	|| no "PCIe2 power/control is not 'on'" "MODEM-4"
lsmod 2>/dev/null | grep -q mhi_sahara_modem \
	&& ok "mhi_sahara_modem loaded" || na "mhi_sahara_modem not loaded" "modem may not be powered"
if dmesg 2>/dev/null | grep -q "MISSION MODE"; then
	ok "modem reached mission mode"
else
	na "modem has not reached mission mode" "modem services are opt-in"
fi

head_ "Known hazards"
if systemctl is-active --quiet kebab-otg.service 2>/dev/null; then
	no "kebab-otg.service is running" "MODEM-5: it fights the MP2762A driver over i2c"
else
	ok "kebab-otg.service not running"
fi

echo
echo "== $pass passed, $fail failed, $skip skipped"
[ "$fail" -eq 0 ] || echo "   Read the named items in docs/REGRESSIONS.md."
exit 0
