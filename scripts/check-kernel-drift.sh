#!/bin/sh
# Has anything replaced our kernel or its modules?
#
#   scripts/check-kernel-drift.sh        (run ON the device)
#
# apk owns /boot/vmlinuz, the DTBs and every file under
# usr/lib/modules/<release>/. If an apk operation restores the
# postmarketOS repo kernel over our build, the symptom is not an error --
# it is Wi-Fi, audio, camera and the modem quietly disappearing after the
# next reboot, because the modules no longer match the running kernel's
# BTF. This checks for that before it bites.
#
# Exit 0 clean, 1 drift found.
set -u

PKG=linux-postmarketos-qcom-sm8250
bad=0

ok()  { echo "  OK    $1"; }
no()  { echo "  DRIFT $1"; bad=1; }
note(){ echo "  --    $1"; }

echo "== running kernel"
run_v=$(uname -v)
run_r=$(uname -r)
echo "  $run_r  $run_v"

echo "== installed package"
inst=$(apk list -I 2>/dev/null | awk -v p="^$PKG-" '$1 ~ p {print $1; exit}')
if [ -z "$inst" ]; then
	no "$PKG is not installed at all"
else
	echo "  $inst"
	case "$inst" in
		*-7.2.0-r0\ *|*-7.2.0-r0) no "the installed package is the repo build (r0), not a local one" ;;
		*) ok "a local build is installed" ;;
	esac
fi

echo "== world pin"
# apk-tools 3 can hold two kinds of constraint for one package: a readable
# version pin ("pkg=7.2.0-r21") and its own content hash, which it writes
# when the package came from a local file ("pkg><<hash>="). Either alone
# satisfies apk; the version pin is the one that clearly blocks a move to a
# repo build, so that is what this looks for.
pins=$(grep -E "^$PKG([=<>!~]|$)" /etc/apk/world 2>/dev/null || true)
if [ -z "$pins" ]; then
	no "no entry in /etc/apk/world -- apk is free to move the kernel"
else
	printf '%s\n' "$pins" | sed 's/^/  /'
	if printf '%s\n' "$pins" | grep -qE "^$PKG=[0-9]"; then
		ok "pinned to an exact version"
	elif printf '%s\n' "$pins" | grep -q "><"; then
		note "only apk's content-hash constraint, no readable version pin"
	else
		no "present but unpinned (no '=version')"
	fi
fi

echo "== does apk want to change the kernel?"
for act in "upgrade --simulate" "fix --simulate"; do
	out=$(apk $act 2>&1 | grep -c "$PKG" || true)
	if [ "$out" = "0" ]; then
		ok "apk $act leaves it alone"
	else
		no "apk $act would touch $PKG"
		apk $act 2>&1 | grep "$PKG" | sed 's/^/        /' | head -3
	fi
done

echo "== module tree matches the running kernel?"
md="/usr/lib/modules/$run_r"
if [ ! -d "$md" ]; then
	no "$md does not exist"
else
	n=$(find "$md" -name '*.ko*' -type f 2>/dev/null | wc -l)
	echo "  $n modules in $md"
	# A module that will not load is the thing that actually hurts.
	if dmesg 2>/dev/null | grep -q "failed to validate module"; then
		no "dmesg shows BTF module-validation failures -- modules do not match this kernel"
	else
		ok "no BTF validation failures in dmesg"
	fi
	owner=$(stat -c %U "$md" 2>/dev/null)
	if [ "$owner" = "root" ]; then
		ok "module tree is root-owned"
	else
		note "module tree owned by '$owner' -- hand-synced rather than installed by apk"
	fi
fi

echo "== /boot staging (not what boots; boot_a is flashed separately)"
if [ -f /boot/vmlinuz ]; then
	note "/boot/vmlinuz $(stat -c %y /boot/vmlinuz 2>/dev/null | cut -d. -f1)"
	note "/boot/boot.img $(stat -c %y /boot/boot.img 2>/dev/null | cut -d. -f1)"
	note "these only matter if something reflashes from the rootfs"
fi

echo
if [ "$bad" = 0 ]; then
	echo "== no drift"
else
	echo "== DRIFT FOUND -- re-run scripts/device-pin-kernel.sh from the builder"
fi
exit "$bad"
