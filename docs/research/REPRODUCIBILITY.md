# Reproducibility notes

## Captured baseline

The aport snapshot identifies:

- package: `linux-postmarketos-qcom-sm8250`
- kernel version: `7.2.0`
- package release: `18`
- device: OnePlus 8T / `kebab`

The engineering handoff recorded a local kernel-tree HEAD of `e5db13c6f` for the
tested worktree.

## Kernel aport completeness

The public repository contains:

- the captured `APKBUILD`;
- `config-postmarketos-qcom-sm8250.aarch64`;
- all **81 patch files referenced by the active `source=` block**;
- five additional historical patch files that were present in the working aport
  directory but are not active in `source=`.

Run:

```sh
./scripts/check-aport-snapshot.sh
```

to verify file presence and SHA-512 consistency against the included `APKBUILD`.
The upstream kernel source tarball itself is not vendored; the `APKBUILD` fetches
it from the configured postmarketOS GitLab URL.

### Publication-only checksum change

`0072-kebab-cci1-pin-clock-rate-37500000.patch` originally carried a local-only
author address in its email-style patch header. The public copy replaces it with
`kebab bring-up <local@invalid>`. Because this changes the patch bytes, the
matching SHA-512 entry in the included `APKBUILD` was updated as part of the
privacy scrub. No functional patch content was changed.

## SDX55 userspace reproducibility

The phone-side collection recovered the tested EFS/tqftp/UIM systemd units and
ModemManager udev rules. A later builder-side collection then recovered the
source/package material for the locally built helpers:

- exact local `kebab-modem-tools` APKBUILD + sources (`pm_service_native`,
  `diag_reader`);
- exact local `tqftpserv-sdx55` APKBUILD + sources;
- the GPL `mhi_efs_sync.c` source from the Apollo modem-tools reference package,
  with its recorded builder commit and build command.

Run:

```sh
./scripts/check-userspace-sources.sh
```

to verify the local package checksum blocks and the recovered MHI EFS source.

The phone's ELF binaries are still not vendored. Their SHA-256 hashes are kept
in `userspace/PROVENANCE.md` only as identification of the tested installation.
Bit-for-bit reproduction of those ELF hashes would additionally require the
same compiler/toolchain, build flags, strip behavior, and package environment.
The **source-level userspace setup is now reviewable and rebuildable** from this
repository.

The local pmaports package directories were captured as uncommitted additions in
a worktree based on pmaports HEAD
`98188c70fb59e1ffa86e4dd8ee1dbae872b9e34a`. The Apollo package reference was
recorded at commit `4c52c8bc8f8bd23a0e31dcd944a15f838777c3e2`.

## What this repository can reproduce

The repository is suitable for:

- reviewing and rebuilding the complete active kernel aport against its matching
  upstream kernel source;
- rebuilding the captured local `pm_service_native` and `tqftpserv-sdx55`
  packages;
- rebuilding `mhi_efs_sync` from the recovered GPL source and applying the tested
  kebab service configuration;
- reconstructing the tested ModemManager/EFS/UIM integration;
- extracting smaller upstreamable board fixes from the accumulated experiment
  series.

No proprietary firmware is distributed. A real device installation still needs
the firmware normally required by the postmarketOS SM8250 port and this device.

The SIM PIN and device-local IMEI restoration helper/service are intentionally
excluded and must never be committed.

## Applying the work

For maintainers, the safest route is to start from the matching postmarketOS
SM8250 aport/kernel baseline, then import only the patch subset being reviewed.
The full kernel sequence contains temporary diagnostics, reverts and historical
no-op/cancel pairs.

Do not assume patch numbers are a clean dependency graph. Read
[`PATCH_SERIES.md`](PATCH_SERIES.md) and the relevant subsystem document first.
