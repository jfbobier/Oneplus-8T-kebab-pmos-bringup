# OnePlus 8T (kebab) — postmarketOS 7.2 / SM8250 bring-up

Community handoff for a heavily patched **postmarketOS / Linux 7.2.0 SM8250
baseline** used on the OnePlus 8T (`kebab`, KB2003). The repository captures the
working research baseline, complete local kernel aport snapshot, SDX55 userspace
source/configuration, and the technical findings behind the front-camera and
modem work.

**This is not presented as an upstream-ready patch series.** The accumulated
kernel stack contains experiments, reverts and board-specific workarounds, and
the cellular radio is not yet online. The purpose is to make the working state
reviewable and reproducible enough for postmarketOS and SM8250/SDX55 maintainers
to reuse the useful pieces.

## Snapshot status — 2026-09-22

| Area | Status | Notes |
|---|---|---|
| Front camera (IMX471) | **Working** | Probe, media graph, CSI reception and capture verified. Six stacked software issues were identified; see [`docs/CAMERA.md`](docs/CAMERA.md). |
| SDX55 boot / mission mode | **Working and stable** | Sahara boot and mission mode stay up with the M3/runtime-PM and EFS-channel fixes. |
| SIM detection / PIN | **Working in the tested installation** | Guarded UIM provisioning and PIN automation are included; the SIM PIN itself is never included. |
| SDX55 userspace sources | **Recovered** | Local `pm_service_native` and `tqftpserv-sdx55` package snapshots plus GPL `mhi_efs_sync` source are under [`userspace/`](userspace/). |
| Cellular radio / data | **Not working** | MCFG selects and downloads the configuration, then aborts during apply after stack deactivation. |
| GPS | **Blocked with modem/radio path** | Treated as the same modem configuration/application blocker in the handoff. |
| Rear cameras | **Not brought up** | Sensor/bus map is documented; only IMX471 had an in-tree driver in the tested kernel baseline. |
| Display/touch/Wi-Fi/BT/audio/USB/DP/charging | Patch series present | Included as part of the working baseline; this repo does not claim each patch is independently upstreamable. |

## Repository layout

- `kernel-aport/` — captured `linux-postmarketos-qcom-sm8250` APKBUILD, kernel
  config and complete active patch set.
- `userspace/` — tested SDX55 systemd/udev/UIM configuration plus recovered
  builder-side source/package snapshots.
- `docs/CAMERA.md` — front-camera root causes, final wiring and capture recipe.
- `docs/MODEM.md` — SDX55 boot/EFS/SIM findings and remaining MCFG blocker.
- `docs/PATCH_SERIES.md` — active patch order and historical patches.
- `docs/REPRODUCIBILITY.md` — what can be rebuilt and what still depends on the
  normal device firmware environment.
- `docs/PRIVACY.md` — publication/privacy review and excluded data classes.
- `docs/UPSTREAMING.md` — cleanup/splitting notes for maintainers.
- `docs/archive/TECHNICAL_HANDOFF_2026-09-22.md` — sanitized long-form
  engineering handoff.
- `tools/` — local update/rollback helpers and the offline F3/EXT_MSG parser.
- `scripts/check-aport-snapshot.sh` — verifies active kernel local sources and
  APKBUILD SHA-512 entries.
- `scripts/check-userspace-sources.sh` — verifies the captured local userspace
  APKBUILDs and recovered MHI EFS source.
- `scripts/privacy-scan.sh` — lightweight pre-publication identifier/secret scan.

## Reproducibility summary

The kernel aport is self-contained with respect to its local sources: the
captured `APKBUILD`, kernel config and **all 81 active patch files** are present.
Five additional historical patches from the working aport directory are retained
for traceability.

The SDX55 userspace source gap has also been closed at source level:

- `userspace/packages/kebab-modem-tools/` is the exact captured local pmaports
  package for `pm_service_native` and `diag_reader`;
- `userspace/packages/tqftpserv-sdx55/` is the exact captured local package and
  source tree used for the SDX55 remote-filesystem server;
- `userspace/reference/mhi-efs-sync/` contains the GPL `mhi_efs_sync.c` source
  recovered from the Apollo reference package, with commit/hash/build
  provenance;
- the tested phone-side systemd/udev/UIM configuration is preserved separately
  under `userspace/systemd`, `userspace/udev`, and `userspace/bin`.

The original phone ELF binaries are intentionally not distributed. Their hashes
are recorded only to identify the tested installation. See
[`userspace/PROVENANCE.md`](userspace/PROVENANCE.md) and
[`docs/REPRODUCIBILITY.md`](docs/REPRODUCIBILITY.md).

`0072-kebab-cci1-pin-clock-rate-37500000.patch` had a local-only patch-author
address in its mail header. The public copy uses `kebab bring-up <local@invalid>`
and the matching APKBUILD SHA-512 entry was updated; no functional patch content
was changed.

## High-value findings

### IMX471 front camera

The front camera was not a hardware failure. Six issues stacked on top of one
another: VANA polarity, PM8008 voltage constraints, missing vendor-style rail
sequencing, wrong CCI master, wrong I2C slave address, and CAMSS CSIPHY
supply/lane configuration. The last blocker was the CAMSS endpoint using 1-based
data lanes where the CSIPHY side required 0-based positions.

See [`docs/CAMERA.md`](docs/CAMERA.md).

### SDX55 modem

Two late kernel fixes were especially important:

- `0091-kebab-sdx55-fusion-no-m3.patch` — keeps the SDX55 out of M3 and prevents
  runtime PM from being re-enabled on mission-mode entry.
- `0092-kebab-mhi-bind-efs-channel.patch` — exposes the MHI EFS channel so
  userspace can service the flashless RMTEFS firmware build.

The recovered userspace shows the other half of the working setup:

- `mhi_efs_sync` services the EFS channel;
- `tqftpserv-sdx55` serves the modem remote filesystem on QRTR instance 3 and
  includes retry behavior for transient send failures;
- `pm_service_native` supplies the Peripheral Manager QMI service;
- ModemManager rules keep the EFS transport out of normal control-port probing;
- the UIM helper activates the primary-GW provisioning session and safely
  automates PIN verification when configured locally.

The remaining blocker is **not modem boot or SIM detection**. MCFG selects and
transfers the configuration, requests protocol-stack deactivation, then returns
to idle instead of completing apply/reactivation. See [`docs/MODEM.md`](docs/MODEM.md).

## Validate the snapshot

From the repository root:

```sh
./scripts/check-aport-snapshot.sh
./scripts/check-userspace-sources.sh
./scripts/privacy-scan.sh
```

## Upstreaming expectations

Useful pieces should be split and reviewed independently. In particular:

- diagnostic/temporary camera patches and their reverts should be dropped from a
  clean series;
- the hard-coded locally administered Bluetooth address in `0014` is a bring-up
  workaround, not a per-device address solution;
- `0092` introduces a Qualcomm-specific EFS WWAN port and is not upstreamable as
  that generic abstraction;
- local power-sequencing changes around the backported IMX471 driver should be
  reconciled with current upstream;
- the tqftpserv source fixes are separable from kebab's `modem_a`/systemd
  integration and may be useful beyond this board.

See [`docs/UPSTREAMING.md`](docs/UPSTREAMING.md).

## Privacy / firmware policy

This public bundle intentionally contains **no SIM PIN value, IMEI, ICCID,
EFS/NV dump, DIAG/QMDL capture, modem firmware image, Android partition image,
private key, password, or access token**. A phone-side helper containing the
factory IMEI was detected during collection and excluded in full.

Do not add raw `.qmdl`, modem `.mbn`, partition `.img`, EFS/NV dumps, or
`/etc/kebab-sim-pin` to this repository.

## Licensing

Repository-authored material is distributed under **GPL-2.0-only** via the
top-level [`LICENSE`](LICENSE). Kernel patches and imported source retain their
own copyright/SPDX notices. In particular, `userspace/packages/tqftpserv-sdx55`
is **BSD-3-Clause**, not relicensed to GPL; see
[`LICENSES/BSD-3-Clause.txt`](LICENSES/BSD-3-Clause.txt).
