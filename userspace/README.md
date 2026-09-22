# SDX55 userspace for the tested kebab installation

This directory contains the public-safe userspace pieces recovered from the
working OnePlus 8T (`kebab`) installation **and** the builder-side source/package
snapshots for the three locally built SDX55 helpers.

The modem result is not kernel-only: userspace must service the SDX55 remote
filesystem/EFS paths, and ModemManager must not probe the EFS transport as a
normal control port.

## Tested phone configuration

- `systemd/mhi-efs-sync.service` — starts the remote-EFS helper once the MHI EFS
  device appears.
- `systemd/tqftpserv-sdx55.service` — serves remote-filesystem requests using
  kebab's A/B `modem_a` layout.
- `systemd/kebab-uim-provision.service` — runs UIM provisioning before
  ModemManager.
- `udev/77-mm-ignore-sdx55-efs.rules` — prevents ModemManager from probing the
  EFS channel.
- `udev/77-mm-sdx55-fusion.rules` — tags the SDX55 WWAN/net ports for the
  Qualcomm SoC ModemManager path.
- `bin/kebab-uim-provision.sh` — activates the primary-GW UIM provisioning
  session and optionally verifies PIN1 from a root-only local file.

The public UIM unit deliberately omits the original dependency on a device-local
IMEI restoration service. That private helper embedded the phone's factory IMEI
and is excluded completely.

## Recovered builder source/packages

- `packages/kebab-modem-tools/` — exact local pmaports package for
  `pm_service_native` and `diag_reader` (`GPL-2.0-only`).
- `packages/tqftpserv-sdx55/` — exact local pmaports package for the patched SDX55
  remote-filesystem server (`BSD-3-Clause`).
- `reference/mhi-efs-sync/mhi_efs_sync.c` — GPL source recovered from the Apollo
  modem-tools package used as the reference for the installed `mhi_efs_sync`.
- `PROVENANCE.md` — builder commit IDs, Apollo-vs-kebab relationship, tested
  phone binary hashes, and licensing notes.

Run:

```sh
./scripts/check-userspace-sources.sh
```

from the repository root to validate the two captured local APKBUILD checksum
blocks and the recovered `mhi_efs_sync.c` hash.

## What changed from Apollo

The captured C sources for `pm_service_native`, `diag_reader`, and the tqftpserv
source files are byte-identical to their Apollo package counterparts. The
kebab-specific work is primarily package/service integration:

- do not package/use the Apollo ESOC `mdm_helper_native` path on kebab;
- use the SDX55 tqftpserv source that publishes QRTR instance 3 and retries
  transient send failures;
- mount the A/B device's `modem_a` partition and expose its `modem_pr` tree;
- use the tested kebab EFS and UIM startup ordering.

See `PROVENANCE.md` for exact details.

## PIN handling

`bin/kebab-uim-provision.sh` reads the first line of `/etc/kebab-sim-pin` only
when that file exists. The PIN file is intentionally absent from this repository
and must remain root-only (`0600`). The script refuses automatic verification
when fewer than all three PIN1 retries remain, so a wrong stored PIN is not
retried on every boot.

SIM/PIN automation is a working feature, but later testing falsified PIN timing
as the cause of the remaining MCFG/radio failure.

## Tested versions

See `metadata/tested-versions.txt`. The captured phone used ModemManager
`1.25.95`, qmicli `1.39.0`, and kernel `7.2.0`.

## Historical/regressive files intentionally excluded

The phone contained a policy-manager restoration experiment that final testing
showed could stall UIM/MCFG state handling. It is not shipped as working
configuration. Purely diagnostic crash/USB logging helpers were also omitted
because they are unnecessary for reproducing the camera/modem fixes and carried
local machine paths.

No SIM PIN, IMEI restoration helper, EFS/NV dump, QMDL capture, proprietary
modem firmware, or phone ELF binary is included here.
