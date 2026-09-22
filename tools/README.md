# Tools

## `qmdl-f3.py`

Offline parser for uncompressed Qualcomm DIAG `EXT_MSG` / F3 (`0x79`) packets from a raw QMDL capture. It does not decode QSR4-compressed messages.

Raw QMDL files can contain device/subscriber information. Do not commit captures to this repository.

## `kebab-update.sh`

Local convenience script that rebuilds/flashes the SM8250 kernel and synchronizes matching modules to the phone. Defaults to the postmarketOS USB-network address `172.16.42.1` and user `user`; override with `PHONE=` and `PHONE_USER=`.

## `kebab-rollback.sh`

Companion rollback helper for saved `boot.img` + module snapshots. Review paths and fastboot assumptions before using it on another installation.

These scripts are included as workflow references, not as postmarketOS-supported tooling.
