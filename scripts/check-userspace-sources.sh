#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

check_apkbuild() {
    dir=$1
    echo "== $(basename "$dir") =="
    (
        cd "$dir"
        awk '
            /^sha512sums="/ { in_sums=1; next }
            in_sums && $0 == "\"" { exit }
            in_sums { sub(/^[ \t]+/, ""); print }
        ' APKBUILD | sha512sum -c -
    )
}

check_apkbuild "$ROOT/userspace/packages/kebab-modem-tools"
check_apkbuild "$ROOT/userspace/packages/tqftpserv-sdx55"

MHI="$ROOT/userspace/reference/mhi-efs-sync/mhi_efs_sync.c"
EXPECTED=13cbae192e01561f37c1959cc2283e2f3f6f57f344a8f0e42edbce6373ba4ba2
ACTUAL=$(sha256sum "$MHI" | awk '{print $1}')
[ "$ACTUAL" = "$EXPECTED" ] || {
    echo "mhi_efs_sync.c: FAILED (expected $EXPECTED, got $ACTUAL)" >&2
    exit 1
}
echo "mhi_efs_sync.c: OK"
