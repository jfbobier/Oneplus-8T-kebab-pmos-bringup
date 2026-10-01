# OnePlus 8T (kebab) — refactored postmarketOS tree

A streamlined rebuild of the OnePlus 8T (`kebab`, KB2003 — SM8250 plus a
discrete SDX55 modem) bring-up for postmarketOS on the
`linux-postmarketos-qcom-sm8250` 7.2.0 kernel.

The bring-up stack was 81 active kernel patches: a journal of experiments,
diagnostics, reverts and board description, in the order it was discovered.
This tree is the same working state expressed as what it actually is.

| | Before | After |
|---|---:|---:|
| Kernel patches | 81 | **18** |
| Board devicetree | ~45 patches | **1 file** |
| Amplifier drivers bound to `nxp,tfa9874` | 2 | **1** |
| Debug/diagnostic patches | 22 | **0** |
| Workarounds kept but disabled | 0 | 3 (`kernel/optional/`) |
| Lines in a boot `dmesg` | 3762 | **1251** |

**Built, flashed and verified on hardware on 2026-10-01**:
`scripts/verify-on-device.sh` reports 26 passed, 0 failed, and a
normalised diff of the full boot log against the pre-refactor kernel shows
zero new error, warning or timeout lines.

**Seven of the eighteen patches were found by testing this refactor**, not
carried over from the bring-up stack, and all seven are generic fixes with
no kebab specifics in them:

* `0014` makes DisplayPort link-training fallback reachable at all on
  Type-C boards — it had been dead code because the retry loop gated
  itself on a controller HPD register that always reads DISCONNECTED when
  HPD arrives through a `drm_dp_hpd_bridge`. This made a 4K monitor work
  that had never worked on this device before.
* `0015` fixes a NULL dereference that oopsed the kernel on reading the DP
  test debugfs files.
* `0016`-`0018` stop three `dev_err` messages firing on entirely healthy
  code paths, which is most of the 67% reduction in boot log size.

The two loudspeakers also play in **genuine stereo** for the first time;
`docs/REGRESSIONS.md` AUDIO-3 has the two independent causes and the
evidence for each.

[`docs/REGRESSIONS.md`](docs/REGRESSIONS.md) is the journal: every
behavioural difference, what was verified and how, what was not, and the
exact way back for each one. Read HW-1 for what is still untested — most
of it needs a cable and a person.

## Layout

```
kernel/              linux-postmarketos-qcom-sm8250 aport
  APKBUILD
  config-postmarketos-qcom-sm8250.aarch64
  sm8250-oneplus-kebab.dts       the whole board devicetree, one file
  0001..0018-*.patch             kernel changes that are really kernel changes
  optional/                      dropped workarounds, one cp from being back
device/oneplus-kebab/  device-oneplus-kebab aport (+ -audio, -modem)
packages/              kebab-modem-tools, tqftpserv-sdx55
scripts/               install, build/flash, on-device verification
docs/                  REFACTOR.md   what moved where
                       REGRESSIONS.md the journal
                       KERNEL-BUMP.md moving to a newer SM8250 kernel
                       research/      why the hardware needs what it needs
```

### A note on provenance

Paths beginning `original/` in these documents refer to the 81-patch
bring-up archive this work started from.

Its **research notes are kept**, in [`docs/research/`](docs/research/) —
they record *why* this hardware needs what it needs, which this tree
deliberately does not repeat. `docs/research/MODEM.md` is the one to read
if you are picking up the cellular-data problem, which is still unsolved.

Its **code is not**: the 81 patches, its `userspace/` and its `tools/` are
superseded, and keeping a buildable copy would invite someone to build
from it. Citations beginning `original/kernel-aport/` or
`original/userspace/` therefore point outside this repository; they are
kept so a claim can be traced to its source rather than asserted.

## Build

```bash
scripts/install-to-pmaports.sh
```

That is a dry run: it prints what it would replace in your pmaports
checkout. Then:

```bash
scripts/install-to-pmaports.sh --force
scripts/kebab-build.sh
```

`kebab-build.sh` installs the aports, checksums, builds the kernel and
device packages, flashes, and then **resynchronises `/lib/modules` and
verifies both sides match**. Add `--modem` to build the SDX55 userspace as
well.

The kernel reaches the device as an **apk**, not a tarball, so that apk's
own database agrees with what is installed. Without that, apk still
believes the postmarketOS repo kernel is present — and since it owns 584
files under `usr/lib/modules/` plus `/boot/vmlinuz`, the next `apk fix` or
`apk upgrade` lays the repo kernel's modules over the running one and
Wi-Fi, audio, camera and the modem all vanish on the next boot. See
`scripts/device-pin-kernel.sh`, and `scripts/check-kernel-drift.sh` to
confirm nothing has moved:

```bash
scripts/device-pin-kernel.sh            # from the builder
sudo scripts/check-kernel-drift.sh      # on the device
```

To put the userspace layer on a running device without building apks —
udev rules, systemd units, helper scripts and the UCM profile:

```bash
scripts/install-userspace-on-device.sh   # run this ON the device
```

It is additive: everything lands under `/usr/lib` so any `/etc` override
keeps winning, no unit's enablement is changed, and anything it replaces is
backed up alongside with a `.pre-refactor` suffix.

After flashing, smoke-test the subsystems this refactor touched:

```bash
sudo scripts/verify-on-device.sh   # run this ON the device
```

Each failure names the `docs/REGRESSIONS.md` item to read.

Do not use `pmbootstrap flasher flash_kernel` on its own. It writes
`boot.img` and nothing else, so every module keeps running the previous
build; during bring-up that produced four separate phantom regressions,
including Wi-Fi disappearing after a boot that had worked. The verification
step at the end of `kebab-build.sh` exists to make that impossible to miss.

Useful flags:

```bash
scripts/kebab-build.sh --no-flash       # build + sync modules, no fastboot
scripts/kebab-build.sh --modules-only   # resync modules only
PHONE=192.168.1.20 scripts/kebab-build.sh
```

## Firmware installed by hand

Not in any package; reinstall after any rootfs reflash.

```
/lib/firmware/qcom/a650_{sqe.fw,gmu.bin}        Adreno; needs SQE >= 0x95,
                                                the vendor blob is 0x93
/lib/firmware/qcom/a650_zap.{mdt,b00,b01,b02,elf}   OEM-signed, from vendor.img
/lib/firmware/ath11k/QCA6390/hw2.0/{amss.bin,m3.bin,board-2.bin}
/lib/firmware/qca/{htbtfw20.tlv,htnv20.bin}
/lib/firmware/qcom/sdx55m/*                     from the device's modem.img
```

The SDX55 blobs are OEM-signed and device-specific. They are not
redistributable and are not in this repository.

## Moving to a newer SM8250 kernel

[`docs/KERNEL-BUMP.md`](docs/KERNEL-BUMP.md) is the runbook. The short
version: only `pkgver` changes, and the one genuinely new step is

```bash
scripts/try-patches.sh /path/to/new/kernel/source
```

which reports each of the 18 patches as `APPLIES`, `FUZZ`, `CONFLICT` or
`REDUNDANT`. `REDUNDANT` means upstream has taken the fix — delete the
patch; that is a win, not a loss.

The board devicetree is a whole file, so a kernel bump cannot conflict —
and cannot deliver an upstream fix either.

```bash
scripts/check-dts-drift.sh            # what upstream changed underneath us
```

To re-prove that the consolidated file is equivalent to some reference
build:

```bash
scripts/verify-dtb.sh reference.dtb candidate.dtb /path/to/scripts/dtc/dtc
```

It resolves phandle renumbering, so the output is the handful of real
differences rather than a thousand shifted integers.

## Re-enabling a dropped workaround

Three patches were removed because the thing they worked around was fixed
properly, or because they were symptoms of a bug fixed elsewhere. Each one
names its symptom in its own commit message and in
[`docs/REGRESSIONS.md`](docs/REGRESSIONS.md).

```bash
cp kernel/optional/0001-OPTIONAL-drm-msm-dp-keep-a-bandwidth-margin-*.patch kernel/
# add the filename to source= in kernel/APKBUILD
pmbootstrap checksum linux-postmarketos-qcom-sm8250
```

## Hardware status

Carried over unchanged from the bring-up archive — this refactor changes
how the work is expressed, not what works.

| Area | Status |
|---|---|
| Display, touch, GPU | working |
| Wi-Fi, Bluetooth, NFC | working (Bluetooth address handling changed — BT-1) |
| USB-C, DisplayPort, OTG VBUS | working (two workarounds dropped — DP-1, DP-2) |
| Audio: loudspeakers (both TFA9874 amps) | working, **stereo** — AUDIO-3 |
| Audio: digital microphones (VA macro) | working |
| Audio: in-call routing | needs an earpiece port for callaudiod — AUDIO-UCM |
| Front camera (IMX471) | working |
| Rear cameras | not brought up; no mainline drivers |
| SDX55 boot, SIM, PIN | working and stable |
| Cellular data, GPS | **not working** — MCFG aborts during apply; see `docs/research/MODEM.md` |

## Licensing

Repository-authored material is GPL-2.0-only. Kernel patches and imported
source keep their own SPDX notices. `packages/tqftpserv-sdx55` is
BSD-3-Clause and is not relicensed.

No SIM PIN, IMEI, ICCID, EFS/NV dump, DIAG capture, modem firmware,
partition image, key or token is in this repository, and none should be
added.
