# `mhi_efs_sync` source provenance

This is the source used as the reference for the `mhi_efs_sync` helper in the tested SDX55 setup.

- Source package: `apollo-modem-tools`
- Source repository recorded by the builder: `royka1/Xiaomi-Apollo-pmOS-packages`
- Builder snapshot commit: `4c52c8bc8f8bd23a0e31dcd944a15f838777c3e2`
- Package license: `GPL-2.0-only`
- Source file SPDX tag: `GPL-2.0`
- Captured source SHA-256: `13cbae192e01561f37c1959cc2283e2f3f6f57f344a8f0e42edbce6373ba4ba2`
- Captured source SHA-512: `61b09274581f4bc03fbbda29c36209c0f90b1291be703a3fe5dad377b6d7efe043ccffcf6ecbc85e45736842b6109ecb8e559491e56381637af88527180f7250`

The original Apollo APKBUILD compiled it directly with:

```sh
$CC $CFLAGS $LDFLAGS -o mhi_efs_sync mhi_efs_sync.c
```

The phone-side systemd unit used on kebab is kept separately at
`../../systemd/mhi-efs-sync.service`; it is intentionally not replaced by the
Apollo unit because the tested kebab service contains device-specific safety and
startup behavior.

This reference source is included to make the helper inspectable and rebuildable.
It does not imply that the complete Apollo modem-tools package was installed on
kebab.
