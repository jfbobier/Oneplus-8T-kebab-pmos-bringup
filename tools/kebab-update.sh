#!/usr/bin/env bash
# kebab-update.sh — rebuild the kernel, flash it, AND sync modules.
#
# Never use bare `pmbootstrap flasher flash_kernel` again: it writes only
# boot.img, leaving /lib/modules on the rootfs from the previous build, so
# every =m driver silently stays stale.
#
#   bash kebab-update.sh            build + flash + sync modules
#   MODULES_ONLY=1 bash ...         only resync modules
#   NOFLASH=1 bash ...              build + sync, do not flash
#   NOBACKUP=1 bash ...             skip the pre-flash rollback snapshot
#
# Rollback snapshot (added 2026-09-17):
#   Right before flashing, this pulls whatever boot.img + /lib/modules is
#   CURRENTLY running on the phone (i.e. the thing about to be replaced) and
#   saves it to ~/kebab/rollback/last-proven/, plus a dated copy under
#   ~/kebab/rollback/history/. That gives a one-step-back rollback after
#   every cycle automatically.
#
#   ~/kebab/rollback/baseline/ is a second, separate slot this script never
#   touches on its own -- it's your fixed floor. Promote something to it
#   explicitly with: bash kebab-rollback.sh --promote-baseline [last-proven]
#
#   To actually roll back: bash kebab-rollback.sh baseline|last-proven
#
# Caveat: the snapshot captures whatever is ACTUALLY running on the phone
# right now, which is only meaningful as "last known good" if you only ever
# re-run this script after confirming the previous cycle's result was good.
# If a prior run was interrupted mid-way (flashed but modules not synced),
# the phone may be in an inconsistent state when this snapshot runs -- check
# `bash kebab-rollback.sh --list` if that's a concern before trusting it.

set -eu
PKG=linux-postmarketos-qcom-sm8250
WORKDIR="$HOME/.local/var/pmbootstrap"
CHROOT="$WORKDIR/chroot_rootfs_oneplus-kebab"
PHONE="${PHONE:-172.16.42.1}"
PHONE_USER="${PHONE_USER:-user}"
ROLLBACK_DIR="$HOME/kebab/rollback"

snapshot_current_phone_state() {
    local dest="$ROLLBACK_DIR/last-proven"
    echo "==> snapshotting the phone's CURRENT (about-to-be-replaced) boot.img + modules -> $dest"
    mkdir -p "$dest"
    local tmp
    tmp=$(mktemp -d)

    if ! scp -o ConnectTimeout=15 "$PHONE_USER@$PHONE:/boot/boot.img" "$tmp/boot.img" 2>&1; then
        echo "!! could not fetch current boot.img from the phone -- skipping rollback snapshot"
        echo "!! (NOT aborting the build/flash because of this)"
        rm -rf "$tmp"
        return 0
    fi

    local kver
    kver=$(ssh -o ConnectTimeout=15 "$PHONE_USER@$PHONE" 'uname -r' </dev/null 2>/dev/null || echo unknown)

    if [ "$kver" != "unknown" ]; then
        ssh -o ConnectTimeout=15 "$PHONE_USER@$PHONE" \
            "sudo tar czf - -C / lib/modules/$kver" </dev/null > "$tmp/modules.tar.gz" 2>/dev/null \
            || echo "!! could not fetch current modules from the phone -- snapshot will lack modules.tar.gz"
    fi

    mv "$tmp/boot.img" "$dest/boot.img"
    if [ -s "$tmp/modules.tar.gz" ] 2>/dev/null; then
        mv "$tmp/modules.tar.gz" "$dest/modules.tar.gz"
    else
        rm -f "$dest/modules.tar.gz"
    fi

    {
        printf 'kver=%s\n' "$kver"
        printf 'saved_at=%s\n' "$(date -u +%FT%TZ)"
        printf 'kebab_local_head=%s\n' "$(git -C "$HOME/kebab/camwork/tree" log --oneline -1 2>/dev/null || echo unknown)"
    } > "$dest/INFO.txt"

    rm -rf "$tmp"

    local histdir="$ROLLBACK_DIR/history/$(date -u +%Y%m%d-%H%M%S)"
    mkdir -p "$histdir"
    cp "$dest/boot.img" "$dest/INFO.txt" "$histdir/" 2>/dev/null || true
    [ -f "$dest/modules.tar.gz" ] && cp "$dest/modules.tar.gz" "$histdir/" 2>/dev/null

    echo "==> snapshot saved: $dest (+ dated copy in $histdir)"
}

if [ "${MODULES_ONLY:-0}" != "1" ]; then
    echo "==> checksum"
    pmbootstrap checksum "$PKG"
    echo "==> build kernel"
    pmbootstrap build "$PKG" --force
    echo "==> build device package"
    pmbootstrap build device-oneplus-kebab --force
    if [ "${NOFLASH:-0}" != "1" ]; then
        if [ "${NOBACKUP:-0}" != "1" ]; then
            snapshot_current_phone_state
        else
            echo "==> NOBACKUP=1 set, skipping rollback snapshot"
        fi
        echo "==> flash (phone must be in fastboot)"
        pmbootstrap flasher flash_kernel
        echo "==> put the phone back into pmOS, then press enter to sync modules"
        read -r _
    fi
fi

KVER=$(ls "$CHROOT/lib/modules" | head -1)
echo "==> syncing modules for $KVER"
T=$(mktemp /tmp/kebab-mods-XXXX.tar.gz)
sudo tar cf - -C "$CHROOT" "lib/modules/$KVER" | gzip > "$T"
sz=$(stat -c %s "$T")
[ "$sz" -gt 1000000 ] || { echo "tarball only $sz bytes, aborting"; exit 1; }
scp "$T" "$PHONE_USER@$PHONE:/tmp/kebab-mods.tar.gz"
ssh "$PHONE_USER@$PHONE" \
  "sudo tar xzf /tmp/kebab-mods.tar.gz -C / && sudo depmod -a $KVER && rm -f /tmp/kebab-mods.tar.gz" </dev/null
rm -f "$T"
echo "==> done. Module dates on the phone:"
ssh "$PHONE_USER@$PHONE" "ls -la /lib/modules/$KVER/kernel/drivers/power/sequencing/" </dev/null
echo "==> reboot the phone to pick everything up"
