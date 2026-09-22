# Contributing

Contributions that turn this research snapshot into smaller, reviewable fixes are
welcome.

Please keep changes scoped by subsystem, explain what was tested on real
hardware, and distinguish measured behavior from hypotheses. Avoid adding
proprietary firmware or device/subscriber dumps.

Before opening a contribution:

```sh
./scripts/check-aport-snapshot.sh
./scripts/check-userspace-sources.sh
./scripts/privacy-scan.sh
```

Do not commit SIM credentials, IMEI/ICCID/IMSI values, EFS/NV dumps, QMDL
captures, Android partition images, private keys/tokens, or device-local IMEI
restoration material.

When modifying imported userspace source, preserve its SPDX/copyright notices and
license. In particular, the tqftpserv-derived source remains BSD-3-Clause even
though repository-authored material is GPL-2.0-only.
