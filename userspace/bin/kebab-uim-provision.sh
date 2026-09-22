#!/bin/sh
# Bring the SDX55's SIM all the way to 'ready' EARLY IN BOOT.
#
# Two jobs, both needed, both time-critical:
#
# 1. Activate the primary-GW provisioning session. The modem's automatic
#    provisioning items are absent on this device --
#      /nv/item_files/modem/uim/mmgsdi/slot_automatic_provisioning   ENOENT
#      /nv/item_files/modem/uim/mmgsdi/subscription_provisioning     ENOENT
#      /nv/item_files/modem/mmode/sub_config                         ENOENT
#    -- so it detects the card but never binds it to a subscription. Without a
#    session the app stays 'detected', PIN1 stays 'not-initialized', and
#    ModemManager reports "GW primary session index unknown" -> sim-missing.
#
# 2. Verify PIN1, if a PIN is stored, so the SIM is ready automatically before
#    ModemManager starts. Later testing showed that PIN timing is NOT the cause
#    of the remaining radio/MCFG failure: the modem still reaches the same MCFG
#    abort with the card ready. Keep UIM/PIN automation and radio diagnosis as
#    separate concerns.
#
# PIN source: /etc/kebab-sim-pin, first line, mode 0600 root:root.
#
# SAFETY: PIN1 allows only 3 attempts before PUK. This script verifies at most
# once per boot, and REFUSES ENTIRELY unless retries are at the full 3. A wrong
# stored PIN therefore costs one attempt on one boot and then stops, instead of
# bricking the SIM. The PIN is never written to the log.
set -u
DEV="qrtr://3"
PINFILE=/etc/kebab-sim-pin
Q="qmicli -p -d $DEV"

card_status() { timeout 20 $Q --uim-get-card-status 2>/dev/null; }

S=""
i=0
while [ $i -lt 60 ]; do
    S=$(card_status) || S=""
    case "$S" in *"Card state: 'present'"*) break ;; esac
    i=$((i + 1))
    sleep 2
done
case "$S" in
    *"Card state: 'present'"*) echo "card present after $((i * 2))s" ;;
    *) echo "card never reported present after $((i * 2))s - nothing to do"; exit 0 ;;
esac

# ---- 1. provisioning session ----
if echo "$S" | grep -qE "Primary GW:[[:space:]]+slot"; then
    echo "primary-gw session already exists"
else
    AID=$(echo "$S" | grep -A1 'Application ID' | sed -n '2p' | tr -d ' \t:')
    if [ -z "$AID" ]; then
        echo "card present but no AID in card status - cannot activate"
        exit 1
    fi
    echo "activating primary-gw slot 1 (aid ${#AID} hex chars)"
    timeout 30 $Q --uim-change-provisioning-session=\
"activate=yes,session-type=primary-gw-provisioning,slot=1,aid=$AID" 2>&1
    S=$(card_status)
fi

# ---- 2. PIN1 verification, guarded ----
if [ ! -r "$PINFILE" ]; then
    echo "no $PINFILE - skipping PIN auto-verify (SIM may remain PIN-locked)"
elif ! echo "$S" | grep -q "PIN1 state: 'enabled-not-verified'"; then
    echo "PIN1 not awaiting verification - skipping"
else
    RET=$(echo "$S" | sed -n "s/.*PIN1 retries: '\([0-9]*\)'.*/\1/p" | head -1)
    RET=${RET:-0}
    if [ "$RET" -lt 3 ]; then
        echo "REFUSING to auto-verify: PIN1 retries=$RET, need 3 (PUK-lock guard)."
        echo "Unlock once by hand, then this will resume next boot."
    else
        PIN=$(head -n1 "$PINFILE" | tr -d ' \t\r\n')
        if [ -z "$PIN" ]; then
            echo "$PINFILE is empty - skipping"
        else
            echo "verifying PIN1 (retries=$RET, one attempt only)"
            OUT=$(timeout 30 $Q --uim-verify-pin=\
"PIN1,$PIN,session-type=primary-gw-provisioning" 2>&1)
            echo "$OUT" | sed -e "s/$PIN/<pin>/g" | head -4
            PIN=""
            unset PIN
        fi
    fi
    S=$(card_status)
fi

echo "final:"
echo "$S" | grep -E "Primary GW:|Application state:|PIN1 state:" | sed -E 's/[0-9]{9,}/<masked>/g'
echo "capability:"
timeout 20 $Q --dms-get-capabilities 2>/dev/null | grep -E 'Networks'
