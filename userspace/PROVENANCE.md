# SDX55 userspace source provenance

The final builder-side collection recovered the source/package trees behind the
locally installed SDX55 helpers. This resolves the earlier source-provenance gap
without redistributing the phone's ELF binaries.

## Exact local kebab package snapshots

`packages/kebab-modem-tools/` is the exact captured local pmaports package from
`temp/kebab-modem-tools/`. It builds:

- `pm_service_native` — Peripheral Manager QMI service used by the SDX55;
- `diag_reader` — diagnostic F3 reader (not required for normal operation).

The package deliberately does **not** build Apollo's `mdm_helper_native`; its
APKBUILD records that kebab has no ESOC driver and uses device-tree GPIO handling
instead.

`packages/tqftpserv-sdx55/` is the exact captured local pmaports package from
`temp/tqftpserv-sdx55/`. It builds the QRTR remote-filesystem server used by the
SDX55 and includes the kebab-specific service definition.

Both local APKBUILD checksum blocks validate against the files included here.
The local package directories were uncommitted additions in a pmaports worktree
whose recorded base HEAD was:

`98188c70fb59e1ffa86e4dd8ee1dbae872b9e34a`

## Apollo source relationship

The builder also contained the Apollo reference package tree at commit:

`4c52c8bc8f8bd23a0e31dcd944a15f838777c3e2`

The following captured files are byte-identical between the Apollo package and
the local kebab package:

- `pm_service_native.c`
- `diag_reader.c`
- `pm-service-native.service`
- `tqftpserv.c`
- `translate.c`
- `zstd-decompress.c`
- `list.h`
- `translate.h`
- `zstd-decompress.h`

The important kebab differences are therefore integration/package choices and
service layout rather than forks of those C source files. For tqftpserv, the
source already contains the SDX55 changes described by the package: publishing
QRTR instance 3 and retrying transient send failures. The kebab systemd unit then
adapts the firmware mount/path for the A/B `modem_a` partition.

## `mhi_efs_sync`

The tested phone also used `mhi_efs_sync`, but the minimal local
`kebab-modem-tools` APKBUILD did not package it. Its GPL source was recovered
from the Apollo `apollo-modem-tools` package and is included under
`reference/mhi-efs-sync/`, together with the exact build command and source
hashes.

The tested kebab systemd unit is kept under `systemd/mhi-efs-sync.service`.

## Tested phone binary hashes

These hashes identify the binaries from the tested phone. The binaries
are intentionally not committed; the source/package material above is the
reviewable artifact.

| Binary | SHA-256 |
|---|---|
| `mhi_efs_sync` | `174184dff988747ca995e4e555cd714e1dc43749a40008e8e0cfb037fe73d31e` |
| `tqftpserv-sdx55` | `ad975c0ea269c5111b478033d0f7982c10b5ef61edb09cb9a69ead12401a13e6` |
| `pm_service_native` | `c0939c16067498bf046b41625ef5411a979f2da534cc6105e303b0e940dcdc71` |

A rebuilt binary is not expected to have the same hash unless compiler,
toolchain, flags, stripping, and package environment are also identical.

## Licensing

- `kebab-modem-tools`: `GPL-2.0-only` (source files carry GPL SPDX tags).
- `mhi_efs_sync.c`: GPL SPDX tag; sourced from the GPL-2.0-only Apollo package.
- `tqftpserv-sdx55`: `BSD-3-Clause`; source files retain their upstream
  copyright/SPDX notices. See `../../LICENSES/BSD-3-Clause.txt`.

The repository's top-level GPL-2.0-only license applies to repository-authored
material; it does not overwrite the BSD-3-Clause license of tqftpserv-derived
files.

## Package service vs tested phone service

The local `tqftpserv-sdx55` package contains its captured builder-time service
file, while `systemd/tqftpserv-sdx55.service` is the service recovered from the
tested phone. They are intentionally both retained. The phone copy contains
later kebab integration details (including its tested `modem_a` mount/firmware
path and invocation), so replacing it with the package service would lose part
of the known-good runtime state.

Likewise, `systemd/mhi-efs-sync.service` is the tested phone unit; the recovered
Apollo source is included to rebuild the helper, not to overwrite that kebab
runtime configuration.
