#!/usr/bin/env bash
# kebab-rollback.sh — flash a previously-saved known-good boot.img (+
# matching modules) back onto the phone. The undo button for a bad
# kebab-update.sh cycle.
#
# Two saved slots live in ~/kebab/rollback/:
#   baseline/     a fixed floor. Never touched automatically -- only changes
#                 when you explicitly run --promote-baseline.
#   last-proven/  what was running immediately before the most recent
#                 kebab-update.sh flash. Updated automatically every cycle.
# Plus ~/kebab/rollback/history/<timestamp>/, a dated copy kept every cycle,
# never pruned (cheap: ~20-50MB each).
#
# Usage:
#   bash kebab-rollback.sh baseline
#   bash kebab-rollback.sh last-proven
#       Flash that saved boot.img via fastboot, then (once the phone is back
#       on the network) restore the matching /lib/modules alongside it, so
#       you don't land in the boot.img-without-matching-modules trap.
#
#   bash kebab-rollback.sh --list
#       Show what's saved in each slot and in history/, with dates.
#
#   bash kebab-rollback.sh --promote-baseline [last-proven|<history-dir-name>]
#       Copy a saved state over baseline/. Defaults to promoting the current
#       last-proven. This is the only thing that ever changes baseline/.
#
# Flashing is a direct `sudo fastboot flash boot <img>`, matching what
# `pmbootstrap flasher flash_kernel` does for this device (deviceinfo does
# not override the kernel partition name, so it's the fastboot default
# "boot"). The phone must already be in fastboot mode.

set -eu
ROLLBACK_DIR="$HOME/kebab/rollback"
PHONE="${PHONE:-172.16.42.1}"
PHONE_USER="${PHONE_USER:-user}"

usage() {
    sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
}

list_saved() {
    for name in baseline last-proven; do
        d="$ROLLBACK_DIR/$name"
        echo "$name:"
        if [ -f "$d/boot.img" ]; then
            sed 's/^/    /' "$d/INFO.txt" 2>/dev/null || echo "    (no INFO.txt)"
            echo "    boot.img: $(du -h "$d/boot.img" | cut -f1)"
            [ -f "$d/modules.tar.gz" ] && echo "    modules.tar.gz: $(du -h "$d/modules.tar.gz" | cut -f1)"
        else
            echo "    (empty)"
        fi
        echo
    done
    echo "history/ (dated, never auto-pruned):"
    if [ -d "$ROLLBACK_DIR/history" ] && [ -n "$(ls -A "$ROLLBACK_DIR/history" 2>/dev/null)" ]; then
        ls -1 "$ROLLBACK_DIR/history" | sort | sed 's/^/    /'
    else
        echo "    (empty)"
    fi
}

promote_baseline() {
    local from="${1:-last-proven}"
    local src="$ROLLBACK_DIR/$from"
    [ -d "$src" ] || src="$ROLLBACK_DIR/history/$from"
    [ -f "$src/boot.img" ] || { echo "no saved boot.img at $src"; exit 1; }

    mkdir -p "$ROLLBACK_DIR/baseline"
    cp "$src/boot.img" "$ROLLBACK_DIR/baseline/boot.img"
    if [ -f "$src/modules.tar.gz" ]; then
        cp "$src/modules.tar.gz" "$ROLLBACK_DIR/baseline/modules.tar.gz"
    else
        rm -f "$ROLLBACK_DIR/baseline/modules.tar.gz"
    fi
    {
        echo "promoted_from=$from"
        echo "promoted_at=$(date -u +%FT%TZ)"
        cat "$src/INFO.txt" 2>/dev/null
    } > "$ROLLBACK_DIR/baseline/INFO.txt"

    echo "==> baseline is now a copy of $from:"
    sed 's/^/    /' "$ROLLBACK_DIR/baseline/INFO.txt"
}

do_rollback() {
    local which="$1"
    local d="$ROLLBACK_DIR/$which"
    [ -f "$d/boot.img" ] || { echo "no saved image at $d -- nothing to roll back to"; exit 1; }

    echo "==> about to flash: $which"
    sed 's/^/    /' "$d/INFO.txt" 2>/dev/null
    echo
    read -r -p "Phone in fastboot mode, and this is really what you want? [y/N] " ans
    case "$ans" in
        y|Y) ;;
        *) echo "aborted"; exit 1 ;;
    esac

    echo "==> flashing boot.img"
    sudo fastboot flash boot "$d/boot.img"
    echo "==> rebooting"
    sudo fastboot reboot

    if [ -f "$d/modules.tar.gz" ]; then
        echo "==> waiting for the phone to come back, to restore matching modules"
        up=0
        for _ in $(seq 1 30); do
            if ping -c1 -W3 "$PHONE" >/dev/null 2>&1; then up=1; break; fi
            sleep 5
        done
        if [ "$up" = "1" ]; then
            scp -o ConnectTimeout=15 "$d/modules.tar.gz" "$PHONE_USER@$PHONE:/tmp/kebab-rollback-mods.tar.gz"
            kver=$(grep '^kver=' "$d/INFO.txt" 2>/dev/null | cut -d= -f2)
            ssh -o ConnectTimeout=15 "$PHONE_USER@$PHONE" \
                "sudo tar xzf /tmp/kebab-rollback-mods.tar.gz -C / && sudo depmod -a ${kver:-} && rm -f /tmp/kebab-rollback-mods.tar.gz" </dev/null
            echo "==> modules restored to match $which (kver=${kver:-unknown})"
        else
            echo "!! phone did not come back over the network within 2.5 min."
            echo "!! boot.img IS flashed, but matching modules were NOT restored."
            echo "!! once it's reachable: scp $d/modules.tar.gz to it, tar xzf -C / on the phone,"
            echo "!! then sudo depmod -a \$(grep ^kver= $d/INFO.txt | cut -d= -f2)"
        fi
    else
        echo "!! no modules.tar.gz saved alongside this boot.img -- flashed boot.img only."
        echo "!! if drivers behave oddly after this, re-run kebab-update.sh MODULES_ONLY=1"
        echo "!! from a chroot that actually matches this kernel, or expect drift."
    fi
    echo "==> rollback to $which complete"
}

case "${1:-}" in
    baseline|last-proven) do_rollback "$1" ;;
    --list) list_saved ;;
    --promote-baseline) promote_baseline "${2:-last-proven}" ;;
    -h|--help|"") usage ;;
    *) usage ;;
esac
