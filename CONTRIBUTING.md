# Contributing

Contributions that turn this work into smaller, reviewable, upstreamable
fixes are welcome — that is most of the point of the refactor.

Please keep changes scoped by subsystem, explain what was tested on real
hardware, and distinguish measured behaviour from hypotheses. This tree
has already been bitten several times by a plausible explanation that
turned out to be wrong; `docs/REGRESSIONS.md` records those, and the
reason each one was caught was a measurement, not an argument.

## Before opening a contribution

```sh
scripts/try-patches.sh /path/to/pristine/kernel/source   # series still applies cleanly
scripts/check-dts-drift.sh                               # upstream devicetree changes
```

and, with the device attached and flashed:

```sh
sudo sh scripts/verify-on-device.sh     # expect 26 passed, 0 failed
sudo sh scripts/check-kernel-drift.sh   # expect "no drift"
```

If you change a kernel patch, regenerate the whole `sha512sums` block in
`source=` order — abuild pairs the two lists *positionally*, not by
filename. See BUILD-3 in `docs/REGRESSIONS.md`.

## Prefer configuration to a kernel patch

Several things that were patches in the bring-up stack are now a
devicetree property, a modprobe option, a udev rule or a UCM profile. If a
change can be expressed as configuration, it should be. Patches are for
code that genuinely does not exist and for generic bugs — nine of the
eighteen are candidates for upstream as they stand.

## Nothing private, ever

Do not commit SIM credentials, IMEI/ICCID/IMSI values, EFS/NV dumps, QMDL
captures, DIAG captures, Android partition images, modem firmware, private
keys or tokens, or device-local IMEI restoration material. `.gitignore`
blocks the common shapes, but it is not a substitute for looking.

Device-identifying values do not belong here either: the Bluetooth address
quoted in `docs/REGRESSIONS.md` is redacted for that reason.

## Licensing

Repository-authored material is GPL-2.0-only. Kernel patches and imported
source keep their own SPDX notices — in particular the consolidated
devicetree is BSD-3-Clause, and `packages/tqftpserv-sdx55` is
BSD-3-Clause and is not relicensed. Preserve SPDX and copyright notices
when modifying imported source.
