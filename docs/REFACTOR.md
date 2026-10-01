# What moved where

> **Provenance note.** Paths beginning `original/` refer to the 81-patch
> bring-up archive this work started from. Its research notes are kept in
> [research/](research/); its code — the 81 patches, `userspace/` and
> `tools/` — is deliberately not in this repository, so those citations
> are historical pointers, kept so a claim can be traced to the file it
> came from.


The bring-up stack was 81 active kernel patches (plus 5 inactive ones kept
for traceability). This tree is:

| Layer | Count | What |
|---|---:|---|
| Kernel `.config` | 1 file | drivers and features that exist, enabled |
| Board devicetree | 1 file | everything that is board wiring |
| Kernel patches | 18 | changes that are genuinely missing C code or generic bugs |
| Optional patches | 3 | dropped workarounds, kept one `cp` away |
| Userspace | 3 packages | policy, service startup, per-device identity |

`original/` is the unmodified bring-up archive and is never written to.

For getting this work into postmarketOS and mainline, see
[UPSTREAMING.md](UPSTREAMING.md) — note that most of it does **not** go to
pmaports: that project's sm8250 kernel aport carries no patches at all.

To move this tree onto a newer SM8250 kernel, see
[KERNEL-BUMP.md](KERNEL-BUMP.md). The short version: only `pkgver`
changes, and the one new step is `scripts/try-patches.sh`, which tells you
which of the 18 patches upstream has since made redundant.

## Why the devicetree is a file and not a patch

About 45 of the 81 patches existed only because pmaports has to reach into
`arch/arm64/boot/dts/qcom/sm8250-oneplus-kebab.dts`. They are a chronology
of discoveries about the board, not a set of independent changes, and half
of them modify lines the other half added.

`kernel/sm8250-oneplus-kebab.dts` is the whole file, organised by
subsystem, and `prepare()` copies it over the in-tree one. The trade is
explicit: a kernel bump can no longer produce a patch conflict, and can no
longer deliver an upstream devicetree improvement either. Run
`scripts/check-dts-drift.sh` after every bump to see what changed
underneath.

The consolidation was verified, not assumed: the DTB compiled from this
file was compared against the DTB from the original 81-patch stack with
phandle renumbering resolved (`scripts/verify-dtb.sh`). Five differences,
all intentional, all in [REGRESSIONS.md](REGRESSIONS.md).

## The 18 kernel patches

| # | Patch | Why it cannot be configuration |
|---|---|---|
| 0001 | `drm/panel`: Samsung AMB655X | no in-tree driver for this panel |
| 0002 | `input`: Synaptics TCM oncell | no in-tree driver for the S3908 |
| 0003 | `regulator`: MP2762A OTG VBUS | no driver provides this regulator |
| 0004 | `drm/msm/dp`: wide-bus vs link bandwidth | generic validator bug |
| 0005 | `usb/typec`: DP altmode defers instead of failing | generic lifecycle bug |
| 0006 | `ASoC`: NXP/Goodix TFA2 (TFA9874) | `tfa989x` covers only TFA1 parts |
| 0007 | `ASoC`: wcd938x AMIC HPF init | generic codec bug |
| 0008 | `media`: IMX471 import (verbatim upstream) | predates this kernel fork |
| 0009 | `media`: IMX471 OF + three-rail sequencing | upstream knows only `vana` |
| 0010 | `bus/mhi`: Sahara v2 loader | flashless modems have no other boot path |
| 0011 | `bus/mhi`: SDX55 Fusion profile | PCI ID collides with a Foxconn module |
| 0012 | `bus/mhi`: honour `no_m3` at mission mode | generic runtime-PM bug |
| 0013 | `net/wwan`: EFS port type | no port type exposes MHI channel 10/11 |
| 0014 | `drm/msm/dp`: link-training fallback vs the HPD block | generic driver bug |
| 0015 | `drm/msm/dp`: NULL deref in the test debugfs files | generic driver bug |
| 0016 | `usb/typec/qcom`: no-op `tx_sig` IRQ logged as an error | generic log-level bug |
| 0017 | `ASoC/wcd938x`: unplugged impedance ramp logged as an error | generic log-level bug |
| 0018 | `remoteproc/qcom_q6v5`: repeated handover logged as an error | generic log-level bug |

Patches 0004, 0005, 0007, 0012, 0014, 0015, 0016, 0017 and 0018 are
generic fixes with no kebab specifics in them, and are the nine most
directly upstreamable. 0016-0018 are pure `dev_err` -> `dev_dbg`
demotions on paths that are reached during normal, healthy operation;
each carries a comment saying why the handler is empty or why the
condition is expected, because that is the part a reviewer needs.

**0014 and 0015 were not part of the bring-up stack.** They were found by
instrumenting this refactor on hardware:

* **0014** — DP link-training fallback (rate and lane down-shift) is
  unreachable on *every* Type-C DP board, because the retry loop gates
  itself on the DP controller's own HPD register, which always reads
  DISCONNECTED when HPD arrives through a `drm_dp_hpd_bridge`. With it, a
  4K monitor that had never worked on this device now trains at 2 lanes
  and drives 2560x1440.
* **0015** — reading `dp_test_active`, `dp_test_data` or `dp_test_type`
  under `/sys/kernel/debug/dri/0/DP-1/` oopses the kernel on external DP,
  because `msm_dp_debug_init()` stores a connector pointer that is NULL at
  that point and never checks it.

**0016-0018 were added after the hardware bring-up was already
working**, to make a healthy boot log readable. Between them they removed
several hundred error-level lines per session that all reported hardware
doing exactly what it had been told to do. 0018 is the promotion of what
used to be `kernel/optional/0004`; the journal entry MODEM-3 anticipated
exactly this.

## Patch-by-patch disposition

`DTS` means the content is a section of `kernel/sm8250-oneplus-kebab.dts`.

| Original | Disposition |
|---|---|
| 0001 enable dsi + amb655x panel | DTS § Display |
| 0002 drm-panel amb655x | patch 0001 |
| 0003 touchscreen s3908 | DTS § Touchscreen, § Display (`&gpu`, framebuffer) |
| 0004 synaptics tcm oncell | patch 0002 |
| 0005 qca6390 wifi/bt | DTS § Wi-Fi / Bluetooth / PCIe |
| 0007 disable unused pcie1 | DTS § Wi-Fi / Bluetooth / PCIe |
| 0008 drop absent hardware | DTS: `pm8009.dtsi` include removed, bq25980 absent |
| 0009 wcd9385 audio | DTS § Audio |
| 0010 q6 dai nodes | DTS § Audio |
| 0011 mhi sdx55 qcom profile | *(inactive)* superseded by patch 0011 |
| 0012 gic mbi for pcie2 | DTS § External modem |
| 0014 qca6390 local bd address | **dropped** → `kebab-bluetooth-addr` (BT-1) |
| 0015 displayport altmode usb-c | *(inactive)* wrong i2c bus, superseded by 0018 |
| 0017 ramoops pstore | DTS § reserved-memory |
| 0018 displayport typec + dp | DTS § USB-C |
| 0019 displayport usb2 host role | DTS § USB-C |
| 0020 typec vbus supply | DTS § USB-C (superseded by 0033) |
| 0021 displayport four lanes | DTS § USB-C |
| 0022 mhi sdx55 fusion sahara | patches 0010 + 0011 |
| 0023 mhi sahara kebab image ids | patch 0010 (image table) |
| 0024 esoc ap2mdm gpio hogs | DTS § External modem |
| 0025 usb hsphy tuning | *(inactive)* not carried |
| 0026 mp2762 otg regulator | patch 0003 |
| 0027 dpu frame-done bailout | **dropped** → `optional/0002` (DP-2) |
| 0028 dp link bandwidth margin | **dropped** → `optional/0001` (DP-1) |
| 0029 dp mode-valid wide bus | patch 0004 |
| 0030 usb3 superspeed | DTS § USB-C |
| 0031 ramoops ecc | DTS § reserved-memory |
| 0032 dpu cap four | folded into `optional/0002` |
| 0033 mp2762 as vbus supply | DTS § USB-C |
| 0034 mp2762 drop bad pinctrl | DTS § Charging (no pinctrl on `charger@5c`) |
| 0035 otg vbus ramp delay | *(inactive)* **reinstated** at 1.2 s (USB-1) |
| 0036 source pdo 1a5 | DTS § USB-C |
| 0037 quiet handover irq | **dropped** → `optional/0004` (MODEM-3) |
| 0038 dp altmode defer | patch 0005 |
| 0039 vbus poll timeout | **dropped** → `optional/0003` (USB-1) |
| 0040 otg ilim 3a | *(inactive)* superseded by the DT current constraint |
| 0041 tfa9874 amp nodes | DTS § USB-C `&i2c15` (`sound-channel` dropped) |
| 0042 tertiary mi2s audio | DTS § Audio |
| 0043 tfa2 codec driver | patch 0006 |
| 0044 tfa9872 alt driver | **dropped entirely** (AUDIO-1) |
| 0045 tert mi2s pinctrl | DTS § Pin configuration |
| 0046 tx-macro debug trace | **dropped** (AUDIO-2) |
| 0047 q6afe debug trace | **dropped** (AUDIO-2) |
| 0048 wcd938x debug trace | **dropped** (AUDIO-2) |
| 0049 bob micbias headroom | DTS § RPMh regulators |
| 0051 wcd938x amic hpf init | patch 0007 |
| 0052 lpass dmic pins | DTS § Audio (`&vamacro`) |
| 0053 va dmic capture | DTS § Audio |
| 0054 dmic clock 2.4 MHz | DTS § Audio |
| 0055 dmic23 pins | DTS § Audio (`&lpass_tlmm`) |
| 0056 pm8008 camera pmic | DTS § Camera |
| 0057 camss camera core | DTS § Camera |
| 0058 imx471 backport | patches 0008 + 0009 |
| 0059 front camera imx471 dt | DTS § Camera, § Pin configuration (DT-5) |
| 0060 vana active-low | superseded by 0077 |
| 0061 reset active-high | superseded by 0063 |
| 0062 pm8008 chip enable gpio93 | DTS § Pin configuration |
| 0063 reset back to active-low | DTS § Camera (final state) |
| 0064 settle-clock diagnostic | **dropped** (CAMERA-1) |
| 0066 vdig 1.1 V exact | superseded by 0080 |
| 0068 revert vdig 1.1 V | n/a |
| 0069 powered reset pulse | absorbed into patch 0009 |
| 0070 / 0071 3 s scan window + revert | **dropped** (cancel pair) |
| 0072 cci1 clock 37.5 MHz | DTS § Camera |
| 0073 / 0074 cci1 master 1 + revert | **dropped** (cancel pair) |
| 0075 / 0076 full bus scan + revert | **dropped** (cancel pair) |
| 0077 vana active-high | DTS § Camera |
| 0078 cci1_i2c0 400 kHz | **dropped** (no-op after 0083) |
| 0079 rescan with vana fixed | **dropped** (diagnostic) |
| 0080 vdig 1.104 V | DTS § Camera |
| 0081 revert rescan | n/a |
| 0082 imx471 vendor power sequence | patch 0009 + DTS § Camera |
| 0083 front camera to cci1 master 1 | DTS § Camera |
| 0084 scan master 1 | **dropped** (diagnostic) |
| 0085 imx471 address 0x10 | DTS § Camera |
| 0086 camss csiphy vdda supplies | DTS § Camera |
| 0087 csiphy settle diagnostic | **dropped** (diagnostic) |
| 0088 csiphy4 lane numbering | DTS § Camera |
| 0089 / 0090 diagnostic reverts | n/a |
| 0091 sdx55 fusion no-m3 | patch 0011 (`.no_m3`) + patch 0012 (the guard) |
| 0092 mhi bind efs channel | patch 0013 |

## Userspace

| Package | Contents |
|---|---|
| `device-oneplus-kebab` | `deviceinfo`, dtbo, per-device Bluetooth address (udev + oneshot + script) |
| `device-oneplus-kebab-audio` | ALSA UCM profile (`install_if` alsa-ucm-conf) |
| `device-oneplus-kebab-modem` | ModemManager udev rules, UIM helper + unit, PCIe2 runtime-PM pin |
| `kebab-modem-tools` | `pm_service_native`, `mhi_efs_sync`, `diag_reader` + their units |
| `tqftpserv-sdx55` | QRTR remote-filesystem server (instance 3, send retry) + unit |

Two things moved out of the kernel into this layer:

* **The Bluetooth address.** A constant in the devicetree is the same
  address on every device. It is now derived per device from
  `/etc/machine-id` and applied with `btmgmt public-addr`, which is the
  supported path for a controller the kernel marks unconfigured. See BT-1.
* **`mhi_efs_sync`.** The unit that starts it shipped without a package
  that builds it. `kebab-modem-tools` now does. See PACKAGING-1.

Three things came *in* from the running device on 2026-10-01, because the
handoff archive predated them:

* **The working UCM profile** (2026-09-16), which has the Speaker device on
  tertiary MI2S and the Mic device on the VA macro DMIC path. The archive
  had no UCM at all and the old copy in `~/kebab/kebab-patches/` was from
  2026-09-04. See AUDIO-UCM.
* **`kebab-pcie2-no-runtime-pm.service`**, which was enabled on the device
  and in no archive. The kernel `no_m3` work is not sufficient without it.
  See MODEM-4.
* **An updated `kebab-uim-provision`**, whose comments correct the
  archive's claim that PIN timing does not matter for the radio. It does:
  MCFG runs once at roughly t+15 s and t+24 s and cannot be retriggered,
  so the card must be ready before then.

`scripts/install-userspace-on-device.sh` installs this layer onto a running
device without building apks. It is additive: everything lands under
`/usr/lib`, never `/etc`, so hand-made overrides keep winning, it changes
no unit's enablement, and it backs up anything it replaces.

## What is deliberately still not upstreamable

* Patch 0011 claims PCI `17cb:0306/17cb:010c`, which a real Foxconn
  T99W175 M.2 module also reports. Telling them apart needs a
  discriminator beyond the PCI IDs.
* Patch 0013 adds a Qualcomm-specific EFS port to a generic WWAN
  abstraction.
* Patch 0006 is a DSP-bypass amplifier driver with no excursion or thermal
  protection — the same trade `tfa989x` documents, but it should say so in
  any submission.
