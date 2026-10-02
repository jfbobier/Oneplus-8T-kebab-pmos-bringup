# OnePlus 8T (kebab) — postmarketOS

Bring-up tree for the OnePlus 8T (`kebab`, KB2003 — SM8250 plus a discrete
SDX55 modem) on postmarketOS with the `linux-postmarketos-qcom-sm8250`
7.2.0 kernel.

20 kernel patches, one board devicetree, and the userspace rules and ALSA
profile the hardware needs. Built, flashed and verified on hardware:
`scripts/verify-on-device.sh` reports 26 passed, 0 failed.

## Hardware status

| Area | Status |
|---|---|
| Display, touch, GPU | working |
| Wi-Fi, Bluetooth, NFC | working |
| Audio: loudspeakers (both TFA9874 amps) | working, in stereo |
| Audio: digital microphones (VA macro) | working |
| Audio: in-call routing | needs an earpiece port for callaudiod |
| USB-C, OTG VBUS | working |
| Battery level reporting | working (`bq27541` fuel gauge) |
| DisplayPort over USB-C | working — 2560x1440, on the native port and through a Thunderbolt dock |
| Audio over DisplayPort | working |
| Front camera (IMX471) | working |
| Rear cameras | not brought up — no mainline drivers |
| Hardware video decode (Venus) | **do not enable** — decodes briefly, then the firmware faults and the device reboots (VIDEO-1) |
| SDX55 modem boot, SIM, PIN | working and stable |
| Cellular data, GPS | **not working** |

**Cellular data is the open problem.** The modem boots to mission mode and
stays up, the SIM is read and the PIN accepted, but MCFG aborts during
apply and the NV — IMEI, RF calibration — lives in an `OEMNVBK` container
rather than in the EFS, so it is not provisioned.
[`docs/research/MODEM.md`](docs/research/MODEM.md) is the full trail of
what was tried; start at §4.4 if you want to pick it up.

Smaller caveats worth knowing before you file a bug:

* **A monitor's USB hub and its picture can be mutually exclusive.** A
  USB-C monitor can carry DisplayPort and USB at the same time only if it
  advertises DP pin assignment D. Many advertise only C and E, which are
  both "four lanes to DP, no USB SuperSpeed" — and such monitors tend to
  drop their USB 2.0 hub as well, so a keyboard and mouse plugged into the
  monitor disappear while the picture is up. The phone's own devicetree
  already advertises C, D and E, so when D is missing it is always the
  monitor. `kebab-dp-mode display|usb|status` switches between the two
  without replugging. DP-3 in the journal, including how to read a
  monitor's capability VDO before blaming it.
* **A dock may take the audio.** A dock in the path usually exposes its
  own USB audio bridge, which PipeWire treats as a perfectly good sink, so
  DisplayPort audio will not be selected for you — pick it explicitly.
* **DisplayPort mode selection.** After the link falls back to fewer lanes
  or a lower rate, the driver still advertises modes sized for the sink's
  maximum rather than the trained link, so a 4K monitor may be offered
  3840x2160 and show black until you pick a lower mode by hand. DP-1/DP-2
  in the journal.
* **Two boot `WARNING`s** at ~0.6 s from the DSI PHY PLL, which taint the
  kernel. They are upstream, harmless, and the display works because the
  failed clock prepare unwinds cleanly. NOISE-1 explains why fixing them
  properly means clk-core surgery.
* **No kernel charger driver.** The `mp2762a` at `i2c-5 0x5c` is claimed
  by this tree's OTG VBUS regulator only, so charge current and charge
  state are not under kernel control and are not reported — only the fuel
  gauge's battery level is. The device does charge; nothing in Linux
  manages it.
* **VAAPI cannot work on this SoC.** Venus is a stateful V4L2 M2M
  decoder; the only VAAPI driver available (`libva-v4l2request`) drives
  stateless Request API decoders, and no VAAPI driver for Venus exists.
  `vaInitialize failed with error code 1` is the correct answer, not a
  misconfiguration. Hardware decode via V4L2 M2M does partly work but
  crashes the device — see VIDEO-1.
* **Rear cameras and in-call audio routing** are not brought up at all.
* **Some firmware must be installed by hand** — see below. It is
  OEM-signed and device-specific, so it is not in this repository.

## Prebuilt images

If you have a kebab and would rather not build a kernel, the
[latest release](../../releases/latest) has `boot.img`, the kernel
modules and the userspace packages, built from this tree and used on a
real device.

You still need pmbootstrap: the rootfs that goes on `super` carries your
username, password, hostname and SSH keys, so nobody else can build it
for you. The release covers the kernel side only.

Order matters, and it is not the obvious one:

1. `pmbootstrap init` and `pmbootstrap install` — build the rootfs
2. `pmbootstrap flasher flash_rootfs` and `flash_dtbo`
3. `pmbootstrap flasher flash_kernel` — the **stock** kernel, and boot it
4. install the kernel apk from the release over SSH, and pin it
5. `fastboot flash boot boot.img` from the release, reboot, then add the
   userspace packages

Step 3 is the one people will want to skip. Most of this device is kernel
modules, and modules that do not match the running kernel are rejected —
so flashing this `boot.img` onto a rootfs still carrying the postmarketOS
repo kernel's modules loses USB networking, Wi-Fi, audio and the modem at
once. The screen and touchscreen are built in, so you end up looking at a
working display with no way to copy the modules across. Booting the stock
kernel once gives you the network that step 4 needs.

The release notes have the full version, including the firmware blobs you
have to extract from your own device.

## Layout

```
kernel/              linux-postmarketos-qcom-sm8250 aport
  APKBUILD
  config-postmarketos-qcom-sm8250.aarch64
  sm8250-oneplus-kebab.dts       the whole board devicetree, one file
  0001..0020-*.patch             kernel changes that are really kernel changes
  optional/                      dropped workarounds, one cp from being back
device/oneplus-kebab/  device-oneplus-kebab aport (+ -audio, -modem)
packages/              kebab-modem-tools, tqftpserv-sdx55
scripts/               install, build/flash, on-device verification
docs/                  REFACTOR.md   what moved where
                       REGRESSIONS.md the journal
                       KERNEL-BUMP.md moving to a newer SM8250 kernel
                       research/      why the hardware needs what it needs
```

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

## Firmware

### Most of it is just three packages

Seven of the files this device needs are redistributable and already in
Alpine/postmarketOS. Verified byte-identical to the working set on the
device (sha256, 2026-10-02), so there is nothing to extract for these:

```bash
sudo apk add linux-firmware-ath11k linux-firmware-qcom linux-firmware-qca
```

| from | files |
|---|---|
| `linux-firmware-ath11k` | `ath11k/QCA6390/hw2.0/{amss.bin,m3.bin,board-2.bin}` |
| `linux-firmware-qcom` | `qcom/{a650_sqe.fw,a650_gmu.bin}` |
| `linux-firmware-qca` | `qca/{htbtfw20.tlv,htnv20.bin}` |

These are redistributable because Qualcomm granted it explicitly — the
ath11k directory ships a `Notice.txt` saying so, which is why
linux-firmware can carry them at all.

Note the packaged `a650_sqe.fw` is **newer than the vendor's**: this
device needs SQE >= 0x95 and the OxygenOS blob is 0x93, so the upstream
one is the one you want regardless.

### What you really do have to extract

Only these, and only because they are OEM-signed and device-specific —
no distribution has the right to ship them:

```
/lib/firmware/postmarketos/{adsp,cdsp,slpi,venus}.mbn
                                        named by firmware-name in the
                                        devicetree; adsp.mbn is the audio
                                        DSP, so without it there is no
                                        audio at all
/lib/firmware/qcom/a650_zap.{mdt,b00,b01,b02,elf}
                                        OEM-signed GPU zap shader, from
                                        vendor.img
/lib/firmware/sdx55m/*                  from the device's modem.img; no
                                        qcom/ prefix — the Sahara loader
                                        in patch 0010 names them
                                        sdx55m/*.mbn
```

Pull them from your own device's `vendor.img` and `modem.img`. They are
not in this repository and will not be: this tree references all firmware
**by filename only**, and never contains a blob.

Nothing above is owned by an apk package once installed by hand
(`apk info --who-owns` reports no owner), which is why the extracted set
has to be reinstalled after a rootfs reflash. The three packages, by
contrast, survive as normal packages.

## Moving to a newer SM8250 kernel

[`docs/KERNEL-BUMP.md`](docs/KERNEL-BUMP.md) is the runbook. The short
version: only `pkgver` changes, and the one genuinely new step is

```bash
scripts/try-patches.sh /path/to/new/kernel/source
```

which reports each of the 20 patches as `APPLIES`, `FUZZ`, `CONFLICT` or
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

## Documentation

| | |
|---|---|
| [`docs/REGRESSIONS.md`](docs/REGRESSIONS.md) | the journal — every known behavioural quirk, what was verified and how, what was not, and the exact way back for each one. Items are referenced by tag (`AUDIO-3`, `DP-1`, `MODEM-2`, …) from the verification script and the commit history. |
| [`docs/REFACTOR.md`](docs/REFACTOR.md) | what lives where, and why each of the 20 patches cannot be configuration instead |
| [`docs/KERNEL-BUMP.md`](docs/KERNEL-BUMP.md) | moving this tree onto a newer SM8250 kernel |
| [`docs/UPSTREAMING.md`](docs/UPSTREAMING.md) | getting this work into postmarketOS and mainline |
| [`docs/research/`](docs/research/) | why the hardware needs what it needs — modem, camera and general findings from the bring-up |

This tree started as 81 incremental kernel patches and is the same working
state consolidated: the board description became one devicetree, the
diagnostics and reverts went away, and policy that had been patched into
the kernel became packaged configuration. Eleven of the remaining 20
patches are generic fixes with no kebab specifics in them and are
candidates for upstream as they stand — including the one that makes
audio over DisplayPort work at all, which is an ASoC/DRM ordering fix
that applies to any board whose CPU-side DAI needs the sink's audio clock
running before it can start.

Paths beginning `original/` in the documents above refer to that
81-patch archive. Its research notes are kept, under
[`docs/research/`](docs/research/); its code is not, because it is
superseded. Citations beginning `original/kernel-aport/` or
`original/userspace/` therefore point outside this repository, and are
kept so a claim can be traced to its source rather than asserted.

## Licensing

Repository-authored material is GPL-2.0-only. Kernel patches and imported
source keep their own SPDX notices: the consolidated devicetree is
BSD-3-Clause, as board devicetrees upstream are, and
`packages/tqftpserv-sdx55` is BSD-3-Clause and is not relicensed. Both
licence texts are in [`LICENSES/`](LICENSES/) and [`LICENSE`](LICENSE).

No SIM PIN, IMEI, ICCID, EFS/NV dump, DIAG capture, modem firmware,
partition image, key or token is in this repository, and none should be
added.
