#!/usr/bin/env bash
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
status=0
scan() {
    label=$1
    pattern=$2
    shift 2
    echo "== $label =="
    if grep -RInE --binary-files=without-match --exclude='privacy-scan.sh' "$@" "$pattern" "$ROOT"; then
        status=1
    else
        echo "no matches"
    fi
    echo
}

# APKBUILD and MANIFEST contain long cryptographic checksums that can include long
# decimal-only runs by chance, so exclude them from the numeric-identifier pass.
scan "long decimal identifiers (review as possible IMEI/ICCID/IMSI)" \
    '(^|[^0-9])[0-9]{15,22}([^0-9]|$)' \
    --exclude='APKBUILD' --exclude='MANIFEST.sha256'
scan "private keys / common token forms" \
    'BEGIN (RSA |OPENSSH |EC |DSA )?PRIVATE KEY|AKIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9_]{20,}|Bearer[[:space:]]+[A-Za-z0-9._-]+'
scan "known local identity remnants" \
    'jeff@|/home/jeff|10\.199\.93\.49|kebab\.local'
scan "credential assignments" \
    '(password|passwd|api[_-]?key|secret|token)[[:space:]]*[:=][[:space:]]*[^[:space:]]+'

if [ "$status" -ne 0 ]; then
    echo "Review matches before publishing. Not every match is necessarily a secret."
    exit 1
fi

echo "No high-risk identifier/credential patterns found by this lightweight scan."
