# Regression journal

> **Provenance note.** Paths beginning `original/` refer to the 81-patch
> bring-up archive this work started from. Its research notes are kept in
> [research/](research/); its code — the 81 patches, `userspace/` and
> `tools/` — is deliberately not in this repository, so those citations
> are historical pointers, kept so a claim can be traced to the file it
> came from.


Every behavioural difference between the 81-patch bring-up stack and this
refactor, with the symptom to watch for and the exact way back. Nothing in
here is speculative: each item says what was verified and what was not.

The refactor was verified three ways before anything was dropped:

1. **Devicetree.** The consolidated `sm8250-oneplus-kebab.dts` was compiled
   and compared against the DTB produced by the original 81-patch stack,
   with phandle renumbering resolved (`scripts/verify-dtb.sh`). The only
   differences are the five listed under DT-1..DT-5 below.
2. **Kernel patches.** The series was applied to a pristine 7.2.0 tarball
   and every touched file compared byte-for-byte against the refactored
   tree. Re-checked at every revision since, including r22.
3. **Compilation.** Every changed or added object was built for arm64 with
   `W=1` and produced no warnings. This was GCC, not the Clang the APKBUILD
   uses — see BUILD-1.

It has since been run on hardware. Read HW-1 first.

**Revision history of this journal.** The series grew from 13 patches to
15 (DP-1/DP-2, found by instrumenting on hardware) and then to 18
(NOISE-2, three log-level demotions). r22 also fixes AUDIO-3, and is the
first revision whose changes had not been verified on hardware at the
time of writing — AUDIO-3 and NOISE-2 say so explicitly.

---

## HW-1 — closed: booted and verified on hardware

**Status:** closed 2026-10-01.

The refactored kernel was built on the Arch builder, flashed to the device
and booted. `scripts/verify-on-device.sh` reports **21 passed, 0 failed**:

| Area | Result |
|---|---|
| Kernel | `#20-postmarketos-qcom-sm8250`, 85 modules, no BTF failures |
| Display | DSI connector `connected` |
| Touch | Synaptics TCM registered |
| Wi-Fi | `wlan` interface present, ath11k up |
| Bluetooth | per-device address `02:XX:XX:XX:XX:XX`, `UP RUNNING` |
| Audio | card present, `snd-soc-tfa2` loaded, `tfa9872` absent on disk and unloaded |
| Audio (session) | PipeWire `HiFi__Speaker__sink` and `HiFi__Mic__source` |
| Camera | IMX471 in the media graph, three-rail sequence ran |
| Charging | `otg-vbus` registered via `mps,mp2762a` |
| USB-C | no `vbus vsafe5v fail` |
| DisplayPort | connector present, no DPU frame-done timeouts |
| Modem | PCIe2 pinned, `mhi_sahara_modem` loaded, mission mode reached |

**The strongest evidence that dropping 68 patches cost nothing:** a
normalised diff of the full boot log against the `#19` baseline shows
**zero** error, warning, timeout or BUG lines on `#20` that were not
already on `#19`.

The one apparent new line, `ramoops: error in header, 1`, is not new:
`#19` logged `ramoops: uncorrectable error in header` four times for the
same benign stale-record condition. pstore registers, is mounted, and the
ramoops console is enabled on both.

Two pre-existing `clk` warnings from `dsi_phy_driver_probe`
(`dsi0_phy_pll_out_dsiclk already disabled`, after `DSI PLL(0) lock
failed`) appear on both kernels. Not a regression, and not investigated.

### Session testing, 2026-10-01 (no reboot, 13 min uptime)

A Lenovo dock, an LG UltraWide over DisplayPort, USB3 peripherals and two
Bluetooth input devices were attached by hand. Results per subsystem:

**Bluetooth — closed.** Both devices paired and trusted:

```
hci0: BD Address: 02:XX:XX:XX:XX:XX   UP RUNNING
      RX bytes:676270 acl:9403 sco:0 events:10543 errors:0
Device F1:01:A7:29:14:0F  BT5.4 Mouse          Paired: yes  Trusted: yes
Device 13:26:AA:BD:EA:CE  Bluetooth Keyboard   Paired: yes  Trusted: yes
```

9403 ACL packets with **zero errors** on the machine-id-derived address.

**USB3 — closed.** The dock's hub chain enumerated at full rate:

```
usb 4-1:   new SuperSpeed Plus Gen 2x1 USB device   VIA Labs USB3.1 Hub
usb 4-1.1: new SuperSpeed Plus Gen 2x1 USB device   VIA Labs USB3.1 Hub
usb 3-1.3: HD Pro Webcam C920
usb 3-1.4: Jabra SPEAK 510 USB        (snd-usb-audio + HID)
sd 1:0:0:0: [sdg] 31129600 512-byte logical blocks: (15.9 GB)
```

SuperSpeed Plus **and** DisplayPort at the same time is pin assignment D,
which is what the board devicetree says to expect. Clean disconnect at
t+772 s, both buses deregistered, no errors. **Zero** USB protocol errors
across the whole session (no `EPROTO`, no failed descriptor reads, no
over-current) — so the un-carried HS PHY tuning (`0025`) is not needed.

**DP-2 — closed.** Roughly eight minutes of live DisplayPort with
**zero** `frame done timeout` lines and no SoC reset. That patch existed
to bound a timeout storm after a failed link train; the link trained and
ran.

**Patches 0004 and 0005 — exercised.** DP and USB3 both came up on a
single attach, which is the altmode/role ordering race `0005` fixes, and
mode validation (`0004`) produced a mode that worked.

**Whole session: zero new kernel warnings.** Only the two pre-existing
boot-time `clk` warnings, identical to `#19`.

### What the session did NOT test

Two journal items are still open, and the session evidence does **not**
close them. Both qualifications matter.

**USB-1 and USB-2 — not tested at all.** The phone was a power *sink*,
not a source:

```
/sys/class/regulator/regulator.20/name  = otg-vbus
                                  state = disabled
                              num_users = 0
(no mp2762 messages anywhere in dmesg)
```

The dock supplies its own power, so the MP2762A boost was never enabled
and the `regulator-enable-ramp-delay` that replaced the Type-C poll
timeout never ran. The "zero `vbus vsafe5v fail`" result is therefore
**vacuous for USB-1** — that warning can only fire while sourcing VBUS.

To actually test it, attach a **bus-powered** device through a plain
USB-C OTG adapter, not a powered dock or hub, so the phone has to source
VBUS. Then check `dmesg | grep vsafe5v` and that the device enumerates on
the first attach rather than ~6 s later.

**DP-1 — only partially tested.** DisplayPort was real and stable, but
probably not in the regime the dropped 85% margin covered.

`~/.config/monitors.xml` records an LG UltraWide at 3440x1440 @ 49.987 Hz
as the primary output next to the internal panel — but that file was
written in an **earlier boot** (nothing in this boot's journal mentions
the monitor), so treat the figure as indicative of the display in use,
not as proof of this session's negotiated mode.

Taking it at face value: with USB3 running simultaneously DP gets two
lanes, so at HBR2 the payload is 2 x 5.4 Gbps x 8/10 = 8.64 Gbps, and
3440x1440 @ 50 at 24 bpp needs roughly 6.2-6.4 Gbps. That is about
**72-74% of the link** — comfortably inside what passes either way. The
dropped margin only changes behaviour between 85% and 100%.

To probe that band, use a 4-lane DP adapter (one that gives up USB3) at
the monitor's highest mode, or a 4K60 panel. If a mode trains and then
shows black, that is DP-1 and
`kernel/optional/0001-OPTIONAL-drm-msm-dp-keep-a-bandwidth-margin-*.patch`
is the answer — try 95% and 90% before 85%.

For a conclusive mode record on the next attach, turn on DRM logging
first (`drm.debug=0x1f` on the kernel command line, or
`echo 0x1f | sudo tee /sys/module/drm/parameters/debug`), then replug and
capture `dmesg | grep -i "link_train\|mode_valid\|bpp"`.

### USB peripherals on a monitor hub: PD discovery fails with this sink

**Diagnosed 2026-10-01, no simple fix.** The monitor has a built-in USB
hub with a keyboard and mouse attached; the phone sees none of it.

The port is in host role and xhci comes up with both buses — but nothing
enumerates downstream, not even a USB2 companion hub:

```
xhci-hcd.6.auto: new USB bus registered, assigned bus number 3
xhci-hcd.6.auto: Host supports USB 3.1 Enhanced SuperSpeed
hub 3-0:1.0: 1 port detected
hub 4-0:1.0: 1 port detected
(...and nothing else, on three separate attaches)
```

The TCPM PD log explains it. With a USB-C host attached at boot,
discovery completes:

```
AMS DISCOVER_IDENTITY finished
AMS DISCOVER_SVIDS finished
  SVID 1: 0xff01        <- DisplayPort
  SVID 2: 0x413c        <- Dell
Alternate mode 0: SVID 0xff01, VDO 1: 0x00001c46
```

With this monitor it does not:

```
AMS DISCOVER_IDENTITY start
AMS DISCOVER_IDENTITY start     <- retry
(never finished)
```

No Discover Identity means no Discover SVIDs, so no DisplayPort alternate
mode is registered — which is why `/sys/class/typec` holds only `port0`
and `port0-partner` and never a `port0-partner.0`. With no altmode there
is no pin-assignment negotiation, so the monitor is never asked for
assignment D (2 DP lanes + USB3) and serves DP across the SuperSpeed pairs
with no USB at all.

There is therefore **nothing to write a pin assignment to**, and no
configuration-level fix. Options, in order of effort:

1. **Bluetooth keyboard and mouse.** Already paired and working.
2. **A different USB-C cable.** VDMs ride on CC; a missing or damaged
   e-marker is a classic cause of Discover Identity timing out. Cheapest
   real test, and success would also likely yield assignment D — which is
   all this board can use anyway, since lanes 2/3 do not train.
3. **A dock.** Demonstrably gives DP and USB3 together on this device.
4. **Chase the VDM failure in TCPM.** Needs the full PD log, which
   `capture-dp-attach.sh` now captures correctly.

Note this is a property of the monitor plus cable, not of the refactor:
the same device enumerated a webcam, a USB speakerphone, mass storage and
nested SuperSpeed Plus hubs through a dock earlier the same day.

### Still untested, needs a person

* **Speaker and microphone audio quality.** The amplifiers were driven 26
  times this session (`tfa2 15-0034` / `15-0035` hw_params, 2 ch 16-bit
  @ 48 kHz) and both probed cleanly, but nothing was assessed by ear.
  `tfa2.c` still has no excursion or thermal protection.
* **Camera frame capture.** The sensor probes and the graph links; no
  frames were pulled. Recipe in `docs/research/CAMERA.md`.
* **Modem data.** Unchanged by this work, still blocked on the MCFG apply
  failure. Modem services remain disabled.

---

## AUDIO-3 — speaker output was mono; two causes, both addressed in r22

**Status:** closed. Fixed in r22 and **verified on hardware** on
2026-10-01 (kernel `#23`). Reverting is cheap and is described at the end
of this entry.

The symptom was that playback was nominally stereo (`PlaybackChannels 2`
in the UCM profile) and actually mono. There were two independent causes,
and only the first had been noticed before.

### Cause 1 — the driver forced both amps onto TDM slot 0

```
tfa2 15-0034: tfa9874 rev 0xc74 ready (channel=0, ...)
tfa2 15-0035: forcing TDM slot 0 (devicetree asked for 1; slot 1 measured as invalid on this board)
tfa2 15-0035: tfa9874 rev 0xc74 ready (channel=0, ...)
```

`tfa2_i2c_probe()` clamped `nxp,tfa-channel` to 0 regardless of what the
devicetree said. The justification recorded in the comment was a
measurement: an amp reading slot 1 showed `CLIPS=70/70` at every gain
setting, while slot 0 showed `CLIPS=0/70` on the same input.

**That measurement was taken under a different fault.** At the time it was
made, the TDM geometry was still programmed once at probe with hard-coded
16-bit values (`NSLOTS=2, SLOTBITS=16, FSBCLKS=32 bclk`), while the
tertiary MI2S backend was actually running `S24_LE` — 32-bit slots, 64
bclk per frame. With that mismatch, "slot 0" happened to land on the top
16 bits of the left sample (real audio, hence clean) and "slot 1" landed
on bits 16..31 of the *same* left sample, i.e. its low-order bits — which
is genuinely full-scale noise. The slot was never the problem; the frame
geometry was.

`tfa2_hw_params()` was later fixed to derive `FSBCLKS`, `NSLOTS`,
`SLOTBITS` and `SWIDTH` from the parameters the backend negotiated, and
to re-apply `TDMSPKS` with them. Nobody re-tested slot 1 after that,
because the probe-time clamp was still in the way. The clamp is now gone,
replaced by a range check (`channel > 1` is a probe error), and the
reasoning above is recorded in the driver next to where the clamp used to
be.

### Cause 2 — the second amp's DAPM supply widget was never powered

This is the one that made the output *single-speaker* mono rather than
dual-mono, and it was visible all along in two lines that had been
written off as cosmetic:

```
tfa2 15-0035: ASoC: sink widget OUT overwritten
tfa2 15-0035: ASoC: source widget POWER overwritten
```

Both amplifiers are separate ASoC components on one card, and the driver
names its DAPM widgets per-instance: `OUT`, `POWER`, `SBSL`, `AIFIN`.
`snd_soc_dapm_add_route()` resolves route endpoints by name across the
whole card's widget list, so with two identically named sets it cannot
tell them apart — that is exactly what "overwritten" is reporting. The
second component's internal routes (`OUT <- SBSL <- POWER`) can therefore
bind to the *first* component's widgets, leaving the second amp's `POWER`
supply widget outside any powered path. A supply widget that is never in
a powered path never runs its event, so that amp never clears `PWDN`.

The fix is configuration, not code: each amp node now carries a
`sound-name-prefix` (`"Left"` and `"Right"`), which ASoC applies to every
widget and control of that component (`soc_set_name_prefix()` reads it
from the component's own `of_node`). The two widget sets become
`Left OUT`/`Right OUT` and so on, the collision is gone, and each
component's routes resolve to its own widgets.

No UCM change was needed: the profile already declares
`PlaybackChannels 2`, and the tfa2 driver registers no ALSA controls, so
prefixing cannot rename anything userspace refers to. The only control
the Speaker device sets is `TERT_MI2S_RX Audio Mixer MultiMedia1`, which
belongs to q6routing and is unprefixed.

### Verified on hardware, kernel `#23`

Each amp takes its own slot:

```
tfa2 15-0034: tfa9874 rev 0xc74 ready (channel=0, stage-1 DSP-bypass, NO speaker protection)
tfa2 15-0035: tfa9874 rev 0xc74 ready (channel=1, stage-1 DSP-bypass, NO speaker protection)
```

The widget collision is gone (0 occurrences), and the prefixes are
visible in the DAPM tree — each component now owns its own widgets:

```
/sys/kernel/debug/asoc/OnePlus 8T/tfa2.15-0034/dapm/  Left AIFIN  Left HiFi Playback  Left OUT  Left POWER  Left SBSL
/sys/kernel/debug/asoc/OnePlus 8T/tfa2.15-0035/dapm/  Right AIFIN Right HiFi Playback Right OUT Right POWER Right SBSL
```

Most importantly, **both** supply widgets now power up during playback,
which is the thing cause 2 was preventing. At idle:

```
Left POWER: Off   Right POWER: Off
```

during `speaker-test -D default -c 2 -t sine -f 440`:

```
Left POWER: On   in 0 out 0 - R0(0x0) mask 0x1
Right POWER: On  in 0 out 0 - R0(0x0) mask 0x1
Left SBSL: On    in 1 out 1 - R0(0x0) mask 0x20
Right SBSL: On   in 1 out 1 - R0(0x0) mask 0x20
```

Playback added no lines to `dmesg` at all. What a register read cannot
confirm is which transducer is which — that needs ears on a hard-panned
tone.

### How to revert

* Dual-mono (both amps, slot 0): set `nxp,tfa-channel = <0>` on
  `tfa9874_right` in the DTS. One line, no rebuild of the patch series.
* Back to the old behaviour entirely: also remove the two
  `sound-name-prefix` properties. The DTB then hashes
  `493bcdf4ff16ae9b52a7be0476eee6454e1b2251edf3afd1f3579b103c0d106d`,
  which is the r19/r21 DTB — that equivalence was checked, and the two
  properties are the *only* structural difference between the r21 and r22
  devicetrees.

### Still true, and still not a stereo problem

This driver is stage-1 DSP-bypass with no excursion or thermal
protection, running NXP's default `TDMSPKG 4/15` and `AMPGAIN 190/255`.
Real stereo does not change that; see the file header in
`sound/soc/codecs/tfa2.c`.

---

## DT-1 — pm8009 is no longer instantiated at all

**Was:** `#include "pm8009.dtsi"`, then `pmic@a` and `pmic@b` set to
`status = "disabled"` and `/delete-node/ &apps_rsc/regulators-0`.

**Now:** the include is gone, so the nodes never exist.

**Why:** identical outcome, one fewer indirection. The PMIC is not
populated on this board; every SPMI read to sid 0xa/0xb fails and RPMh
cmd-db has no `smpf2`, so its rpmh-regulator block cannot register either.

**Risk:** very low. Verified: the DTB comparison shows the pm8009 nodes as
the only structural removal, and they were disabled anyway.

**Back out:** re-add `#include "pm8009.dtsi"` after `pm8150l.dtsi`. Nothing
in the tree references an f-series rail — the Wi-Fi PMU was deliberately
moved onto `vreg_s6a_0p95`.

---

## DT-2 — three unreferenced pinctrl states removed

**Removed:** `bt_en_sleep`, `wlan_en_sleep`, `ts_rst_suspend`.

**Why:** nothing selects them. A pinctrl state only has an effect when a
consumer names it, and no node in this tree does. They were left over from
the vendor-derived dts.

**Risk:** none identified. If sleep states are wanted later for the QCA6390
or the touchscreen, they must be added back *together with* a
`pinctrl-names = "default", "sleep"` on the consuming node — which is what
was missing before.

---

## DT-3 — touchscreen interrupt flags cleaned up

**Was:** `interrupts = <39 0x2008>`. **Now:** `<39 IRQ_TYPE_LEVEL_LOW>`
(`0x8`).

**Why:** `0x2000` is a downstream flag with no meaning in mainline.
`irq_domain_translate_twocell()` masks the second cell with
`IRQ_TYPE_SENSE_MASK`, so the kernel already saw `0x8`. The old value would
additionally trip `WARN_ON(type & ~IRQ_TYPE_SENSE_MASK)` in
`irq_create_of_mapping()`.

**Risk:** none. Provably identical after masking.

**Back out:** put `<39 0x2008>` back in the touchscreen node.

---

## DT-4 — `charger@5c` compatible renamed

**Was:** `op,mp2650-charger`. **Now:** `mps,mp2762a`.

**Why:** the part is a Monolithic Power Systems MP2762A. `op,mp2650` is
OnePlus's downstream name for a different charger. The driver's match table
changed with it, so the pair is consistent.

**Risk:** low, but this is the one rename that will silently produce "no
OTG VBUS" if only half of it is applied. If you take the devicetree from
here and an older driver, or the reverse, the charger will not probe:
`dmesg | grep mp2762` shows nothing and `regulator_summary` has no
`otg-vbus`.

**Back out:** change both the `compatible` in the dts and
`mp2762_otg_of_match[]` in `drivers/regulator/mp2762-otg-regulator.c`.

---

## DT-5 — `cam_mclk4_default` moved out of `sm8250.dtsi`

**Was:** a patch adding the pin state to the SoC dtsi next to
`cam_mclk3_default`. **Now:** defined in the board file under `&tlmm`.

**Why:** it removes a patch against a shared SoC file for something only
this board uses. `&tlmm { ... }` in a board dts appends children to the
same node, so the result is byte-identical.

**Risk:** none. Confirmed present in the compiled DTB.

---

## BT-1 — the Bluetooth address moved from devicetree to userspace

**Was:** patch `0014` hard-coded `local-bd-address = [55 24 f1 37 00 02]`
in the devicetree.

**Now:** no `local-bd-address`. `device-oneplus-kebab` ships
`kebab-bluetooth-addr`, a udev-triggered oneshot that derives a locally
administered address from `/etc/machine-id` and sets it with
`btmgmt public-addr`.

**Why:** one constant in a devicetree is the same Bluetooth address on
every device that flashes this image, which is wrong on a shared radio
medium. The kernel path is supported: `btqca` sets
`HCI_QUIRK_USE_BDADDR_PROPERTY` when it reads the firmware placeholder
`00:00:00:00:5A:AD` back, so with no DT property the controller registers
`HCI_UNCONFIGURED`, and an unconfigured controller is exactly what
`MGMT_OP_SET_PUBLIC_ADDRESS` is for. `hci_qca` provides `hdev->set_bdaddr`
(`qca_set_bdaddr`), which that command requires.

**Verified:** the kernel side, by reading `net/bluetooth/mgmt.c`
(`set_public_address`, `is_configured`), `drivers/bluetooth/btqca.c:727`
and `drivers/bluetooth/hci_qca.c:2080` in this exact tree.

**Verified on device 2026-10-01, and it found two real bugs:**

1. The first version read `/sys/class/bluetooth/hciN/address`. That
   attribute **does not exist** on this kernel — the directory holds only
   `device`, `power`, `reset`, `rfkill*`, `subsystem` and `uevent`. Both
   the wait loop and the "is it already configured?" test were therefore
   broken, and the script exited with "hci0 never appeared".
2. `btmgmt` is at `/usr/sbin/btmgmt`, and systemd's default PATH on this
   image is only `/usr/local/bin:/usr/bin`. `command -v btmgmt` inside the
   unit would have failed.

Both are fixed: the script asks the management interface instead
(`btmgmt --index N info`), decides on the `missing options:
public-address` line — which is empty on a configured controller and lists
`public-address` on an unconfigured one — and searches absolute paths for
the binary. The unit also sets an explicit PATH.

Confirmed working through systemd on the running device, on the
already-configured path:

```
kebab-bluetooth-addr: hci0 already configured (addr 02:00:37:F1:24:55),
                      leaving it alone
Active: active (exited) ... status=0/SUCCESS
```

**Verified on the first `#20` boot, and it found a third bug.** The
controller came up exactly as predicted:

```
hciconfig:      BD Address: 00:00:00:00:5A:AD   DOWN RAW
btmgmt config:  Unconfigured index list with 1 item
                hci0: Unconfigured controller
                  supported options: public-address
                  missing options:   public-address
```

but the script reported "hci0 did not register within 30s". The reason is
a genuine subtlety in the management interface, and it is worth knowing:

* `btmgmt info` reads `MGMT_OP_READ_INDEX_LIST`, which lists only
  **configured** controllers. For an unconfigured one it prints
  `Index list with 0 items` and says nothing about it at all.
* `btmgmt config` reads the **unconfigured** index list, and that is the
  only place a controller with no usable address appears.

So the detection has to consult both: `info` first (already configured ->
do nothing), then `config` (unconfigured for want of an address ->
assign). The wait loop now polls for `/sys/class/bluetooth/$DEV` instead,
since that directory does exist even though it has no `address` attribute.

One more trap: `btmgmt --index 0 config` misprints the index, showing
`hci304:` instead of `hci0:`. Both subcommands list every controller
anyway, so the script passes no `--index` to either and uses it only for
`public-addr`.

**Working end to end as of 2026-10-01:**

```
kebab-bluetooth-addr: assigning 02:e1:69:34:3f:df to hci0
hci0 Set Public Address complete

(the kernel then re-runs QCA setup: htbtfw20.tlv + htnv20.bin reloaded)

hci0: Type: Primary  Bus: UART
      BD Address: 02:XX:XX:XX:XX:XX
      UP RUNNING
btmgmt info: Index list with 1 item, hci0: Primary controller
             current settings: powered ssp br/edr le secure-conn
```

Note the address is **not** `02:00:37:F1:24:55` — that was the devicetree
constant. This one is derived from this device's `/etc/machine-id`, so a
second kebab flashed from the same image gets a different one, which was
the entire point.

Re-addressing costs about 1-2 s because the controller reinitialises and
re-downloads its firmware.

**Symptom:** `hciconfig hci0` shows `BD Address: 00:00:00:00:5A:AD ... DOWN
RAW` and `Can't init device hci0: Not supported (95)`; no adapter in
`bluetoothctl`.

**Diagnose:**

```sh
systemctl status 'kebab-bluetooth-addr@hci0'
journalctl -u 'kebab-bluetooth-addr@hci0'
cat /sys/class/bluetooth/hci0/address
command -v btmgmt
```

**Back out (devicetree, immediate):** add to the `bluetooth` node in the
`&uart6` block of `kernel/sm8250-oneplus-kebab.dts`:

```dts
	local-bd-address = [ 55 24 f1 37 00 02 ];
```

Bytes are reversed, as the binding requires. Then rebuild the kernel.

**Back out (userspace, no rebuild):** run it by hand once and it persists
until reboot:

```sh
sudo btmgmt --index 0 public-addr 02:00:37:f1:24:55
```

---

## DP-1 / DP-2 — dropped workarounds, and what instrumented capture found instead

**Status:** both still dropped. Neither addresses the real fault, which is
now identified. One of them (DP-1) becomes relevant only *after* the real
fault is fixed.

Kept at `kernel/optional/0001-*` (85% bandwidth margin) and
`kernel/optional/0002-*` (DPU frame-done bailout).

### The real fault: lanes 2 and 3 never achieve clock recovery

Captured with `scripts/capture-dp-attach.sh` (DRM debug bit 8), LG
UltraFine 4K direct over USB-C, no dock. `rate=540000` (HBR2),
`num_lanes=4`, `pixel_rate=537600`, mode 3840x2160@60, `bpp = 30`.

Clock recovery swept voltage swing 0 -> 3, reading DPCD 0x202/0x203 each
step (bit 0 per lane = CR_DONE):

| v_level | 0x202 (lanes 0,1) | 0x203 (lanes 2,3) |
|---|---|---|
| 0 | `00` | `00` |
| 1 | `00` | `00` |
| 2 | `01` | `00` |
| 3 | `11` | `00` |

At maximum swing **lanes 0 and 1 both reach CR_DONE; lanes 2 and 3 never
do.** The sink requested swing 1, then 2, then 3 for all four lanes, so it
is seeing nothing at all on the upper pair.

That explains the whole pattern: DisplayPort **works through a dock**,
which negotiates pin assignment D (2 DP lanes + USB3, i.e. lanes 0 and 1),
and **fails connected directly**, which negotiates C or E (4 lanes). Two
of the four DP lanes are not usable on this path.

`/sys/class/typec/port0/orientation` was **`reverse`** for this attach.

**Orientation has been ruled out.** Both cable orientations were tried and
both fail the same way — the pre-reboot attach and this captured one are
opposite orientations, and both end in `max v_level reached`. Only the
captured one has per-lane DPCD detail, so strictly the evidence that
lanes 2/3 specifically fail is from one orientation, but the failure
signature is identical in both. Do not spend another cycle flipping the
cable.

### Open: what the Type-C altmode actually negotiated

**Unknown, and an earlier version of this entry wrongly claimed it was
known.** The first capture reported no DP altmode because
`capture-dp-attach.sh` globbed
`/sys/class/typec/port0-partner/displayport/`. That path does not exist by
design: an alternate mode is its own device and a **sibling** of the
partner, `/sys/class/typec/port0-partner.0/`, and that is where the DP
driver puts `configuration` and `pin_assignment`. The script looked in the
wrong place and found nothing, which is easy to misread as "no altmode".

This matters because **pin assignment is the one value that would settle
the lane question**:

* assignment **C or E** -> 4 DP lanes, no USB3;
* assignment **D** -> 2 DP lanes + USB3.

If the altmode negotiated D while the DP controller went on to configure
4 lanes from the *sink's* DPCD max (which is what `msm_dp_ctrl_on_link`
does — `num_lanes` comes from `ctrl->panel->link_info.num_lanes`), then
lanes 2 and 3 are simply not routed, and the symptom is exactly what was
measured. That would be a real generic bug: the DP controller trusting
sink capability over what Type-C actually gave it.

The script now walks the whole `typec` class and enables dynamic debug for
`typec`, `typec_displayport`, `phy_qcom_qmp_combo`, `qcom_pmic_typec`,
`typec_mux_fsa4480` and `tcpm`, so the next capture answers this.

### DEBUGFS OOPS: patch 0015, and a mistake of mine that found it

**Three kernel NULL-pointer oopses, caused by this project's own capture
script.** Patch `0015` is in the series.

```
Unable to handle kernel NULL pointer dereference at virtual address 0xa8
Internal error: Oops: 0000000096000004 [#1] SMP
pc : msm_dp_test_active_show+0x14/0x60
                              [#2]  msm_dp_test_data_show+0x30/0xe0
                              [#3]  msm_dp_test_type_show+0x24/0x64
```

`0xa8` is the offset of `drm_connector::status`, so `debug->connector` is
NULL. `msm_dp_display_debugfs_init()` passes
`dp->msm_dp_display.connector` to `msm_dp_debug_init()`, which validates
`dev`, `panel` and `link` but **not `connector`** — and on external
DisplayPort the connector does not exist yet, because the bridge creates
it later. All four handlers then dereference it unconditionally.

**How it was triggered:** `scripts/capture-dp-attach.sh` globbed
`/sys/kernel/debug/dri/0/DP-1/*` during collection, which includes
`dp_test_active`, `dp_test_data` and `dp_test_type` — one oops per
`cat`. The `timeout 3 cat` guard was useless because the fault is inside
the read. Each oops killed only the reading task, so the device stayed
usable, but the kernel was left tainted `[D]=DIE`.

The script now whitelists debugfs paths and says in a comment why
`DP-1/*` must never be globbed. Patch `0015` fixes the kernel so the read
cannot oops at all.

A second script bug was found at the same time: the TCPM PD log at
`/sys/kernel/debug/usb/tcpm-*/log` is a **one-shot drain**, and reading it
with `grep` threw away everything after the first match. It is now
captured whole in a single read, and the PD negotiation is summarised.

---

### VALIDATED ON HARDWARE: patch 0014 fixes the fallback

**Confirmed working 2026-10-01 on `#21`.** Patch `0014` is in the series.

Same monitor, same cable, direct USB-C. Before the patch: one attempt,
`-ECONNRESET`, compositor wedged, touch dead. After:

| Attempt | `LINK_BW_SET` | Rate | Lanes | Result |
|---|---|---|---|---|
| 1 | `0x14` | HBR2 5.4G | 4 | `max v_level reached`, fail |
| 2 | `0x0a` | HBR 2.7G | 4 | fail |
| 3 | `0x06` | RBR 1.62G | 4 | fail |
| 4 | `0x14` | HBR2 5.4G | **2** | **training #1 and #2 successful** |

The rate down-shift walked HBR2 -> HBR -> RBR, then
`clock_recovery_any_ok()` passed on lanes 0/1, the lane down-shift halved
the lane count, the rate reset to maximum, and the link trained.

**Zero** frame-done timeouts and **zero** vblank timeouts, against 2-6 and
3-11 on the three pre-patch attaches. The compositor stayed alive and
GNOME detected the monitor. No wedge, no dead touch.

It is a generic fix: nothing in it is board- or monitor-specific, and it
restores fallback on every Type-C DP board.

Note what attempts 2 and 3 prove: lanes 2/3 fail at **1.62 Gbps** as well
as 5.4. A marginally-routed lane trains at RBR. Failing at every rate
means those lanes are not carrying signal at all — with *this* cable.
Bring-up patch `0021` recorded a 4-lane adapter working on this board, so
do **not** conclude the board lacks 4 lanes, and do **not** cap
`data-lanes` in the devicetree.

### Still black: the mode list is not re-evaluated after a fallback

**New, generic, and now the blocking issue.** The link trains but the
screen stays dark, which is what the device showed: "detected in gnome but
no display out".

The committed mode is `3840x2160@60`, pixel clock 537600 kHz, **bpp 30**.
The link that actually trained is **2 lanes at HBR2**:

* available: 2 x 540000 x 8 = **8.64 Gbps**
* required: 537600 x 30 = **16.13 Gbps**
* **187% of the link**

`msm_dp_ctrl_on_stream` duly programmed it anyway, and the transfer-unit
arithmetic overflowed:

```
msm_dp_ctrl_on_stream: rate=540000, num_lanes=2, pixel_rate=268800
msm_dp_panel_timing_cfg: wide_bus_en=1 reg=0x10
msm_dp_ctrl_on_stream: n_sym = 84, num_of_tus = 171
msm_dp_ctrl_on_stream: TU: delay_start_link: 65502     <- 0xFFDE
msm_dp_ctrl_on_stream: TU: tu_size_minus1: 44
```

`delay_start_link = 65502` is a 16-bit wrap of a negative value. The
stream cannot fit, so no valid video is produced.

**Why the mode was offered at all:** `msm_dp_bridge_mode_valid()` sizes
modes against `panel->link_info.num_lanes` — the *sink's* capability
(4, capped only by the devicetree `data-lanes`) — not
`link_params.num_lanes`, the lanes actually trained. The mode list is
built before training, the fallback then halves the link, and nothing
revisits the list. `pixel_rate=268800` in the on_stream line is just
`wide_bus_en=1` halving the DPU datapath rate; the link still carries
every pixel.

**DP-1 does not fix this.** Its 85% margin is also computed on 4 lanes:
17.28 x 0.85 = 14.69 Gbps, so 4K60 would be clamped from 30 bpp to 24 bpp
(12.9 Gbps) and still offered — and 12.9 is still far over the real
8.64 Gbps.

**The fix** is to make the advertised modes reflect the achieved link:
after a fallback reduces lanes or rate, update `panel->link_info` and
re-probe the connector so userspace picks a mode that fits. A weaker
variant is to fail the atomic enable when the trained link cannot carry
the committed mode, which at least turns a black screen into an honest
error.

**Confirmed 2026-10-01: 2560x1440 works.** Selecting it manually in GNOME
Display settings produces a picture, on both cable orientations. ~241 MHz
at 30 bpp is 7.2 Gbps, 84% of the 8.64 Gbps a 2-lane HBR2 link carries.
So the link and the hardware are fine; only the mode *selection* is wrong,
exactly as diagnosed.

What remains is the automatic case: GNOME is still offered 3840x2160@60
because the mode list is sized on the sink's 4 lanes, picks it, and gets a
black screen until the user intervenes. The fix is to re-probe the
connector after a fallback changes the lane count or rate, so the
advertised list reflects the link that actually trained. Not written yet.

### Superseded by the above: fallback is unreachable on every Type-C DP board

**Confirmed 2026-10-01.** This is the generic bug, and it is the one worth
fixing. Shipped as series patch `0014`.

`msm_dp_ctrl_setup_main_link()`'s retry loop is DP fallback: on a training
failure it shifts the rate down HBR3 -> HBR2 -> HBR -> RBR, and then, if
clock recovery succeeded on at least half the lanes, reduces the lane
count. Both failure branches guard that with

```c
if (!msm_dp_aux_is_link_connected(ctrl->aux))
        break;
```

`msm_dp_aux_is_link_connected()` reads `REG_DP_DP_HPD_INT_STATUS` bits
[31:29] — the **DP controller's own HPD block** (dp_aux.c:669). That is
only meaningful when the block is wired to a real HPD pin.

On a USB-C port it is not. HPD arrives out of band through a
`drm_dp_hpd_bridge` owned by the Type-C driver, and the controller's
register reads DISCONNECTED for the whole time a sink is attached. The
driver prints both values side by side in `msm_dp_bridge_hpd_notify()`,
and across both captures **every** notification said so:

```
    4 x  hpd_link_status=0x0, status=1
    1 x  hpd_link_status=0x0, status=2
```

`0x0` is `DP_DP_HPD_STATE_STATUS_DISCONNECTED`. So the guard is always
true, the loop always breaks on the first failed attempt, and **the
rate/lane fallback never runs on any Type-C DP board**.
`msm_dp_ctrl_setup_main_link()` then returns `-ECONNRESET`, which is the
`rc=-104` seen in every attach.

Three attaches, three times exactly one training attempt and no
down-shift. That is the whole explanation for dock-works /
direct-fails: through a dock the altmode negotiates two DP lanes up
front and the first attempt succeeds, so fallback is never needed.
Connected directly, four lanes are tried, lanes 2 and 3 never achieve
CR, and the two-lane fallback that would have worked is unreachable —
`msm_dp_ctrl_clock_recovery_any_ok()` would have passed, because CR was
already good on exactly the lanes that would remain.

**The fix** is to use the DPCD read the loop already performs as the
liveness test instead of the HPD register. It states the intent directly
— a sink that has gone away cannot answer AUX — and carries no assumption
about where HPD comes from. `drm_dp_dpcd_read_link_status()` returns 0 on
success and a negative errno on failure.

Shipped as series patch `0014` and validated on hardware — see the
VALIDATED entry above.

### Superseded note: the lane fallback never runs

This should have recovered on its own. `msm_dp_ctrl_setup_main_link()`'s
retry loop (dp_ctrl.c:2310-2365) is supposed to rate-down-shift
HBR2 -> HBR -> RBR and then, if
`msm_dp_ctrl_clock_recovery_any_ok(link_status, lane_count)` finds CR on
`lane_count >> 1` lanes, call `msm_dp_ctrl_link_lane_down_shift()`.

Here `any_ok` would check lanes 0 and 1 — **both of which had CR_DONE** —
so a drop to 2 lanes would have trained and the monitor would have worked
at a lower mode.

It never got there. Only **one** training attempt was made, then:

```
link training #1 on phy 0 failed. ret=-11
link training on sink failed. ret=-11
Failed link training (rc=-104)              <- -ECONNRESET
Unexpected DP AUX IRQ 0x01000000 when not busy
```

The only path out of that loop before any down-shift is
`if (!msm_dp_aux_is_link_connected(ctrl->aux)) break;`. So AUX/HPD was
judged disconnected immediately after the failed attempt and the entire
rate/lane fallback was skipped.

**This is the generic bug worth fixing** — it would help any marginal link
on any board, not just this monitor. AUX itself was demonstrably healthy
throughout: EDID read cleanly (EDID 1.4, 10 bpc, "LG ULTRAFINE", full
mode list) and every DPCD transaction succeeded.

### Correcting an earlier bandwidth claim

An earlier entry in this journal put the DP mode at ~74% of the link. That
assumed 24 bpp. It is wrong: the sink reports 10 bpc, so the driver uses
**30 bpp** (`msm_dp_panel_init_panel_info: bpp = 30`):

* required: 537600 kHz x 30 = **16.13 Gbps**
* available: 4 x 540000 x 8 = **17.28 Gbps**
* **93.3% of the link**

That is precisely the figure `0028`'s own commit message cites — "a stream
at 93% of that figure trains and then delivers no frames". So **DP-1 does
matter for this monitor**, just not yet: bits per pixel has no bearing on
clock recovery, which is where this attach dies. Fix the lanes first; if
the picture is then black at 3840x2160@60, DP-1 is the next thing to try,
and it would clamp bpp to 24 (-> 74.7%) rather than reject the mode.

### DP-2 still would not have fired

Second confirmation from this capture: **2** frame-done timeouts and 3
vblank timeouts. Same burst-of-two shape as before, and
`frame_done_timeout_cnt` is reset in `dpu_encoder_virt_atomic_enable()`
(dpu_encoder.c:1347), so the `>= 4` consecutive threshold is unreachable.
`0027` used 20 and `0032` lowered it to 4; neither is reachable here.
Consistent with this monitor never having worked before the refactor.

### Ranked next steps

**Done:** patches `0014` and `0015` are in the series and flashed. The
monitor works at 2560x1440 on both cable orientations, with no wedge and
no DPU timeouts.

**Remaining, in order of value:**

1. **Re-probe the connector after a lane/rate fallback** so the advertised
   mode list reflects the link that actually trained. This is the last
   thing between "works after picking a resolution by hand" and "works on
   plug-in". Generic; benefits any sink that needs fallback.
2. **Why Discover Identity fails** with this monitor — see the USB hub
   entry earlier in HW-1. Try a different cable first.
3. **DP-1** (`optional/0001-*`) is *not* needed for this monitor: the
   working 2560x1440 mode sits at 84% of the 2-lane link, just under the
   85% threshold. It becomes relevant again only if step 1 lands and the
   driver starts auto-selecting modes near the limit.

Ruled out, not worth revisiting: cable orientation (both work), and
`data-lanes = <0 1>` in the devicetree (bring-up patch `0021` recorded a
4-lane adapter working on this board, so capping would regress it).

### Operational note

A direct 4-lane DP attach **tears down the USB network gadget** —
`enp0s20f0u2` disappears and the phone leaves `lsusb`, because the port
takes the host/DFP role and pin assignment C/E leaves no USB3. SSH over
USB is therefore impossible while a monitor is directly attached.
`capture-dp-attach.sh` is built to run detached for this reason. For live
debugging, put the device on Wi-Fi instead.

---

## DP-3 — a USB-C monitor's hub and its picture can be mutually exclusive

**Status:** understood and worked around. Not a kebab defect and not
fixable in software — the limit is in the monitor.

**Symptom.** With an LG USB-C monitor attached, DisplayPort works
(2560x1440, 2 lanes HBR2) but the keyboard and mouse plugged into the
monitor's own USB hub never appear. `lsusb` shows only the four root hubs;
the Type-C controller's USB 2.0 root-hub port reads

```
port01: 0x0a0002a0 Speed=0 Link=RxDetect PP WCE WOE
```

— port powered, nothing connected. Not an enumeration failure: no device
is presented at all.

**Cause.** DisplayPort Alt Mode pin assignments decide how the four
SuperSpeed lanes are split. C and E give all four to DP and leave no USB
SuperSpeed; D gives two to DP and keeps USB 3.x. Both ends advertise what
they support in the DP Capability VDO and the kernel uses the
intersection (`dp_altmode_configure()` in
`drivers/usb/typec/altmodes/displayport.c`).

Reading both VDOs settles it:

| | VDO | pin assignments |
|---|---|---|
| phone (DFP_D, bits 15:8) | `0x00001c46` | `0x1c` = **C D E** |
| monitor (UFP_D, bits 23:16) | `0x00140045` | `0x14` = **C E** |

The phone already offers D — the `altmodes` node in
`kernel/sm8250-oneplus-kebab.dts` sets `vdo = <0x00001c46>`. The monitor
does not, so the negotiated set is C and E and there is no configuration,
devicetree change or quirk that can produce USB 3.x alongside DP here.

There is a second part that the spec does *not* require: in pin assignment
C the USB 2.0 pins remain connected, so the hub could still have offered
the keyboard and mouse at high speed. This monitor drops its hub entirely
while DP is configured. That is the monitor's firmware, confirmed by the
test below.

**Proof that nothing else is wrong.** Writing `USB` to the altmode's
`configuration` hands the lanes back and everything appears at once:

```
Bus 003 Device 002: ID 0bda:5411 Generic USB2.1 Hub
Bus 004 Device 002: ID 0bda:0411 Generic USB3.2 Hub
Bus 003 Device 003: ID 30fa:1701 INSTANT USB GAMING MOUSE
Bus 003 Device 004: ID 046a:0113 Cherry GmbH CHERRY Wired Keyboard
Bus 003 Device 005: ID 043e:9a39 LG Electronics Inc. LG Monitor Controls
```

So the cable carries USB data, the phone's host role and USB 2.0 path are
fine, and the hub works. Only the mode is exclusive.

**Workaround:** `kebab-dp-mode`, shipped in `device-oneplus-kebab`:

```bash
sudo kebab-dp-mode display   # picture, no hub
sudo kebab-dp-mode usb       # hub, no picture
sudo kebab-dp-mode status
```

**One trap it encodes.** Writing `sink` back to `configuration` restores
the configuration but does not necessarily re-send a pin assignment: the
altmode ends up configured with *no* lanes, `hpd` goes to 0 and the screen
stays black while the connector still reports `connected`. Writing the pin
assignment afterwards is what actually brings the picture back, so the
`display` path does both in that order. This was hit by hand before the
script existed.

**Before blaming a monitor,** check its VDO — bit 3 of bits 23:16:

```bash
cat /sys/class/typec/port0-partner/port0-partner.*/vdo
```

A monitor that does advertise D should give DP and USB 3.x together with
no changes on this side.

---

## USB-1 — the Type-C vSafe5V poll timeout was replaced by a regulator ramp delay

**Was:** patch `0039` raised the generic Qualcomm Type-C vSafe5V poll
timeout from 250 ms to 1.2 s.

**Now:** the MP2762A boost declares how long it takes, in two places:

* `regulator-enable-ramp-delay = <1200000>` on the `otg_vbus` node;
* `.enable_time = 1200000` in `struct regulator_desc` in the driver, as a
  default for boards that do not set the property.

`regulator_enable()` therefore returns only once VBUS is genuinely up, and
`qcom_pmic_typec_port_vbus_toggle()`'s 250 ms poll — which runs immediately
after that call, verified by reading the function — succeeds first time.

**Why:** teaching a generic Type-C driver that every VBUS supply might be
slow is the wrong layer. The rail is slow; the rail should say so.

**Note on the earlier attempt:** patch `0035` tried exactly this and was
abandoned. It set 300 ms, below the measured 550 ms–1.1 s range, so it
could not have worked. The value here is 1.2 s.

**Symptom:** `vbus vsafe5v fail` from `qcom_pmic_typec_port` on every
source attach, followed by the device enumerating ~6 s later via TCPM's own
retry. That is cosmetic-plus-slow, not broken.

**Diagnose:** the ramp delay is applied by the core, so check it landed:

```sh
grep -r . /sys/class/regulator/*/name | grep otg-vbus   # find the regulator
dmesg | grep -i 'vsafe5v\|mp2762'
```

**Back out:** `kernel/optional/0003-OPTIONAL-usb-typec-qcom-widen-the-vSafe5V-*.patch`.
It is additive — you can apply it *and* keep the ramp delay.

---

## USB-2 — the OTG current limit is now a devicetree constraint

**Was:** `MP2762_REG07_OTG_ILIM_1500MA` compiled into the driver; changing
it meant editing C (which patch `0040` did, to 3 A, and that was reverted).

**Now:** `regulator-min-microamp` / `regulator-max-microamp` on the
`otg-vbus` node, both 1500000 — the same 1.5 A as before. The driver
implements `set_current_limit`/`get_current_limit`, caches the value and
programs REG07 from `.enable`.

**Why caching rather than writing immediately:** the regulator core applies
machine constraints during registration, while the boost is off. REG07 only
has an effect once REG08[5] is set, and writing i2c at registration time
would be a new failure mode in a path that currently works. All register
access stays inside `.enable`, exactly where it was.

**Risk:** low. The programmed value is unchanged.

**If you raise it:** raise the `source-pdos` entry in the connector node in
the same commit. Advertising more than the boost can source browns out the
rail on a sink that believes you.

---

## AUDIO-1 — only one amplifier driver now exists

**Was:** `tfa2.c` (patch `0043`) and `tfa9872.c` (patch `0044`) both
matched `nxp,tfa9874`, and both were `=m`. Whichever loaded first bound the
amps. The devicetree carried `nxp,tfa-channel` *and* `sound-channel` so
either could read its slot.

**Now:** only `tfa2.c`. `tfa9872.c`, `CONFIG_SND_SOC_TFA9872` and the
`sound-channel` property are gone.

**Why tfa2 and not tfa9872:** `tfa2.c` is ported from NXP's own TFA9874
source — hidden-register unlock, the three per-revision tuning tables, the
PWDN→MANSCONF→CLKS→SBSL state machine. `tfa9872.c` is an adaptation of a
different chip's driver with the TFA9874 revision ids bolted on, and does
none of that. The evidence that `tfa2.c` is what was actually exercised on
hardware is in patch `0045`'s own commit message, which quotes `CLKS=0` and
`MANSTATE=1` — both `tfa2.c` concepts — as the symptom it was fixing.

**Not verified:** that the ambiguity was not load-order-dependent in a way
that happened to favour `tfa9872`. If speakers regress, this is the first
thing to test.

**Symptom:** no loudspeaker output; `dmesg | grep tfa` shows the driver
bound but the amps never leave `MANSTATE=1`.

**Back out:** `original/kernel-aport/0044-kebab-tfa9872-alt-driver.patch`
still applies to `sound/soc/codecs/{Kconfig,Makefile}` after series patch
`0006`, but you must also restore `sound-channel = <0>;` / `<1>;` on the
two `speaker-amp@` nodes and set `CONFIG_SND_SOC_TFA2=n`. Loading both
again is not a fix — it is the original bug.

---

## AUDIO-2 — three debug tracing patches removed

**Removed:** `0046` (lpass-tx-macro), `0047` (q6afe / q6afe-dai), `0048`
(wcd938x). All were `dev_info`/`pr_info` calls added to trace the microphone
path.

**Risk:** none functional. If you need them back while chasing a capture
problem they are in `original/kernel-aport/`.

---

## AUDIO-DP — DisplayPort audio is wired correctly and blocked in the ADSP

**Status:** devicetree and UCM work done, flashed as `#24` and verified as
far as it goes. **Playback does not work.** The blocker is identified
precisely and is not in anything this tree owns.

### What was missing, and is now done

Less than expected. The DP controller already registers an HDMI codec —
`msm_dp_bridge_init()` sets `hdmi_audio_dev` and friends, so the DRM
bridge-connector helper calls `drm_connector_hdmi_audio_init()` — and
`sm8250.dtsi` already gives `displayport-controller@ae90000`
`#sound-dai-cells = <0>`. `CONFIG_SND_SOC_HDMI_CODEC=y` was already set,
q6afe-dai already had the `DISPLAY_PORT_RX` DAI (LPASS port 104) and
q6routing already had its mixers. The codec sat unbound because the board
never declared the link.

Added to `kernel/sm8250-oneplus-kebab.dts`: a `displayport-dai-link`
(cpu `<&q6afedai DISPLAY_PORT_RX>`, platform `<&q6routing>`, codec
`<&mdss_dp>`) and `dai@68` under `&q6afedai`. Added to `ucm-HiFi.conf`: a
`SectionDevice."HDMI"` with `JackControl "DP0 Jack"`, declared
`ConflictingDevice` with Speaker because both drive the MultiMedia1
frontend.

All of that works. On `#24` with a monitor attached:

```
numid=181  'DISPLAY_PORT_RX Audio Mixer MultiMedia1' ... MultiMedia8
numid=106  'DP0 Jack'      = on
numid=110  'ELD', device=6 = populated
ASoC DAIs  DISPLAY_PORT_RX_0 .. _7
alsaucm    0: Speaker   1: HDMI   2: Mic
```

`aplay -l` still lists only MultiMedia1/MultiMedia2, which is correct —
`DISPLAY_PORT_RX` is a *backend*. Playback goes to the MultiMedia1
frontend and is routed with the mixer, exactly as the speakers are routed
through `TERT_MI2S_RX Audio Mixer MultiMedia1`.

### Where it fails

```
qcom-q6afe: AFE enable for port 0x6020 failed -110
q6afe-dai: fail to start AFE port 68
q6afe-dai: ASoC error (-110): at snd_soc_dai_prepare() on DISPLAY_PORT_RX_0
```

Port `0x6020` is `AFE_PORT_ID_HDMI_OVER_DP_RX`. The ADSP does not answer
`AFE_PORT_CMD_DEVICE_START` within q6afe's `TIMEOUT_MS` of 3000.

A second attempt in the same boot answers differently:

```
cmd = 0x100e5 returned error = 0x9      (ADSP_EALREADY)
AFE enable for port 0x6020 failed -22
```

So the DSP *did* eventually process the first start and considers the
port running — it just took longer than three seconds. And because the
first start "failed", `q6afe_dai_prepare()` never set
`is_port_started[]`, so it never stops the port before retrying and every
later attempt gets EALREADY until a reboot.

### Why the DSP stalls — the actual root cause

An ftrace of the attempt gives the order directly:

```
q6afe_hdmi_port_prepare  <-q6afe_dai_prepare
q6afe_port_start         <-q6afe_dai_prepare
msm_dp_audio_shutdown    <-drm_bridge_connector_audio_shutdown
```

**`msm_dp_audio_prepare()` is never called.** The DP controller's audio
path is never enabled, so the ADSP is asked to start a port whose clock
is not running, and waits.

The reason it is never called is an ordering dependency:

* `drm_connector_hdmi_audio_ops` provides `.prepare` but **no
  `.hw_params`**, so `hdmi_codec_hw_params()` returns early and *all* DP
  audio enablement happens in the codec DAI's `.prepare`.
* `snd_soc_pcm_dai_prepare()` walks CPU DAIs before codec DAIs and
  aborts on the first error.
* q6afe is the CPU DAI. It runs first, needs the DP audio clock, times
  out, and the loop aborts — so the codec's `.prepare`, and with it
  `msm_dp_audio_prepare()`, never runs.

Chicken and egg, and structural rather than a configuration mistake.

**Corroborating evidence:** no sm8250 board in this kernel wires DP audio
at all. The boards that do (`x1e80100`, `sm8650`, `qcm6490`) use the newer
`q6apm` path; `sc7180-acer-aspire1` is the only legacy-`q6afedai` example
and is a different SoC. This looks like a combination nobody has made work
on this ADSP.

### What to try next, in order of promise

1. **Give the DRM helper a `.hw_params`.** If
   `drm_connector_hdmi_audio_prepare()` ran at hw_params time — which is
   before any DAI's prepare — the DP audio clock would be up when q6afe
   starts the port. The signature already matches what
   `hdmi_codec_hw_params()` passes. This is the cleanest hypothesis and
   the smallest patch, but it is a generic DRM/ASoC change and it is a
   theory: the ADSP may still refuse.
2. **Make the timeout failure recoverable.** Independent of the above,
   `q6afe_dai_prepare()` should mark the port as needing a stop when
   `q6afe_port_start()` times out, so a retry is not permanently poisoned
   by EALREADY until reboot. That is a real defect on its own.

Both are kernel changes to built-in code (`CONFIG_DRM_DISPLAY_HELPER=y`,
`CONFIG_SND_SOC_HDMI_CODEC=y`), so each test costs a full build and a
fastboot flash — which means unplugging the monitor each cycle.

### If you give up on it

Revert cost is small and the leftovers are harmless: the dai-link and the
UCM HDMI device cost nothing when no monitor is attached, and the Speaker
device is unaffected (verified after all of the above — `speaker-test`
still reports Front Left / Front Right in stereo). To remove entirely,
drop `displayport-dai-link` and `dai@68` from the devicetree and the
`SectionDevice."HDMI"` plus Speaker's `ConflictingDevice` from
`ucm-HiFi.conf`.

---

## AUDIO-UCM — resolved: the device had a newer profile than the archive

**Status:** closed 2026-10-01.

The handoff archive contained no UCM profile, and the copy in the old
`~/kebab/kebab-patches/ucm/` tree was from 2026-09-04 — before the TFA9874
and DMIC work. The running device turned out to carry a 2026-09-16 profile
that solves both, and that is what is shipped now:

* **Speaker.** One mixer control is the whole difference between silence
  and sound: `TERT_MI2S_RX Audio Mixer MultiMedia1`. Without it the MI2S
  backend is never enabled, no bit clock reaches the amps, and they sit at
  `CLKS=0` forever. Nothing else sets it, and `alsa-restore` will happily
  restore it as "off", which is why the speaker used to work only
  sporadically.
* **Mic.** Capture goes through the **VA** macro, not the TX macro: in
  mainline only `lpass-va-macro` implements the DMIC clock
  (`va_dmic_clk_enable`, `CDC_VA_TOP_CSR_DMICn_CTL`); `lpass-tx-macro` has
  the `MSM_DMIC` routes but no clock code at all. `VA DMIC MUX0` = `DMIC1`
  matches stock's `handset-mic` -> `dmic2` mapping.
* **No Earpiece device, deliberately.** On this board the earpiece is the
  second stereo transducer, driven by the second TFA9874 — not by the WCD.
  A WCD-routed "Earpiece" points at hardware that is not wired to it, and
  it shared `hw:0,0` with Speaker at a different channel count, which made
  PipeWire respawn a failing Earpiece node in a loop (`prepare` returning
  `-EINVAL`, "Routing not setup for MultiMedia-1"). Both transducers are
  covered by the stereo Speaker device.

**Still open:** `callaudiod` wants an earpiece port, so in-call routing
needs revisiting whenever the modem becomes usable.

**Safety note that still applies:** `tfa2.c` is a DSP-bypass driver with no
excursion or thermal protection.

---

## MODEM-4 — PCIe2 host-controller runtime PM is a userspace requirement

**Status:** carried forward, newly documented.

The kernel `no_m3` work (series patches `0011` and `0012`) stops the *MHI*
M3 suspend. It is not sufficient on its own. The running device also has
`kebab-pcie2-no-runtime-pm.service` **enabled**, and its own comment
records why:

> Patch 0091 stopped the MHI M3 suspend, and ASPM is not implicated, but
> the modem link still dropped at t~708s. The host controller platform
> device `1c10000.pcie` was still `power/control=auto`: pcie-qcom's
> runtime suspend turns off the PIPE clocks and puts the PHY in low power,
> which takes the link with it.

So it writes `on` to `power/control` for `1c10000.pcie`,
`0002:00:00.0` and `0002:01:00.0`.

This was not in the handoff archive and was nearly lost. It is now shipped
and enabled by `device-oneplus-kebab-modem`.

**Symptom if missing:** the modem reaches mission mode, runs fine, and the
link drops at roughly t+708 s — the same signature as the original M3 bug,
one layer down.

**Proper fix, not attempted here:** this belongs in the kernel, either as a
`pcie-qcom` quirk or by having the MHI controller take a runtime-PM
reference on its parent. Treat the unit as a known workaround, not a
solution.

---

## MODEM-5 — `kebab-otg.service` must not be started under the new kernel

**Status:** new hazard introduced by this refactor.

The device has `/etc/systemd/system/kebab-otg.service` (`static`, manual
start only) running `kebab-otg-enable.py --forever`. That script is the
*userspace* MP2762A OTG implementation: it drives PM8150 GPIO3 and writes
REG07/08/09 over i2c directly.

Series patch `0003` makes the kernel own that charger. Starting the script
as well means two writers on the same registers, and the script's exit
path deliberately restores REG07/08/09 and drops GPIO3 — which would
silently kill VBUS underneath the regulator framework, with the regulator
still believing it is enabled.

**Do not start it.** It is `static`, so it will not fire on boot. It is
left in place rather than deleted because it is the fallback if the
MP2762A driver misbehaves — but if you use it, blacklist the driver first:

```sh
echo 'blacklist mp2762_otg_regulator' | sudo tee /etc/modprobe.d/kebab-otg.conf
```

That only works if the driver is a module; the kebab config has
`CONFIG_REGULATOR_MP2762_OTG=y`, so in practice you would need to boot the
previous kernel (`#19`) instead.

---

## MODEM-1 — the `no_m3` fix is now two changes, and half of it was already upstream

**Was:** patch `0091` added a `no_m3` field to `struct mhi_pci_device`, set
`.no_m3 = true` on the SDX55 profile and guarded `pm_runtime_allow()`.

**Now:** `struct mhi_pci_dev_info` already has `no_m3` in this 7.2.0
baseline — it is used at probe for `pci_pme_capable()` and set on
`mhi_qcom_qdu100_info`. So the profile flag is part of series patch `0011`,
and only the `mhi_pci_status_cb()` guard remains as its own change, series
patch `0012`.

**Why it matters:** the guard is the half that actually fixed the 706-second
death, and as a standalone commit it is a straightforward upstream
candidate: probe deliberately declines runtime PM for `no_m3` devices and
the mission-mode callback silently hands it back.

**Risk:** none identified; the combined effect is unchanged.

---

## MODEM-2 — the Sahara loader's logging was turned down

**Changed:** per-`READ_DATA` `dev_info` and two firmware hexdumps became
`dev_dbg` or were removed. The one that dumped the outgoing chunk also read
`sdev->rx->read_data.offset` from a buffer that may already have been
refilled by a later packet, so it could print a stale value.

**Added:** `sdev->rx_size` is now checked against an 8-byte minimum before
the packet is parsed. It was recorded by the DL callback and never used, so
a runt packet would have been parsed out of whatever the buffer held
before.

**Kept deliberately:** an unmapped Sahara image id still falls back to
`sdx55m/qdsp6sw.mbn`, now with a `dev_warn`. Refusing would be stricter and
arguably more correct — handing the SBL an 84 MB payload where it wanted a
12 KB manifest is exactly the failure patch `0023` fixed — but the observed
boot chain uses only mapped ids, and turning an unknown id into a hard boot
failure is a worse trade during bring-up.

**Turned down again in r22:** the surviving per-image `dev_info`
("`Sahara: image N -> loaded <file> (N bytes)`") fired 15 times on every
modem boot. It is now `dev_dbg`, and the loader instead prints one line
per *session* when the modem acknowledges DONE:

```
mhi_sahara_modem mhi0_SAHARA: Sahara: session complete, 15 image(s) / 94371840 bytes sent
```

The counters (`images_sent`, `bytes_sent`) are reset along with the rest
of the session state at `DONE_RESP`, so a second Sahara session reports
its own totals rather than accumulating.

**Symptom of the logging change:** modem boot looks quieter. `dmesg | grep
Sahara` shows one line per session instead of one per image (and, before
that, one per chunk). Add `dyndbg="file sahara.c +p"` to the kernel
command line to get it all back.

---

## MODEM-3 — resolved: the remoteproc handover demotion is back, as patch 0018

**Was:** patch `0037` turned a `dev_err` into `dev_dbg` in
`qcom_q6v5_handover_interrupt()`. The refactor dropped it to
`kernel/optional/0004` as log noise only, with the note "add it back if
the SLPI handover message is drowning out something you need to read".

**Now:** that is exactly what happened, so in r22 it is back in the
default series as patch `0018`, rewritten as an upstream-shaped commit
(the SoC-specific 766-line count is evidence in the message, not a kebab
condition in the code). `kernel/optional/0004` no longer exists.

**Why it is correct and not just quieter:** the early return *is* the
complete response. By the time the sideband is re-asserted the handover
callback has run and interconnect bandwidth has been dropped; nothing is
retried and no state is inconsistent. A condition with no recovery and no
consequence is not an error.

**Back out:** drop `0018-remoteproc-qcom_q6v5-*.patch` from `source=` in
`kernel/APKBUILD` and from `kernel/`.

---

## NOISE-1 — the two clk WARNINGs at 0.64 s are upstream and deliberately not fixed

**Status:** pre-existing, present in every build from r19 onwards and in
the bring-up stack before it. **Not fixed.** This entry exists so nobody
spends the afternoon on it twice.

```
DSI PLL(0) lock failed, status=0x00000000
PLL(0) lock failed
dsi0_phy_pll_out_dsiclk already disabled
WARNING: drivers/clk/clk.c:1188 at clk_core_disable+0xb0/0xd0
  clk_core_disable_lock / __clk_set_parent_after
  clk_core_reparent_orphans_nolock / of_clk_add_hw_provider
  dsi_phy_driver_probe
...
WARNING: drivers/clk/clk.c:1048 at clk_core_unprepare+0xe4/0x11c
```

These two are the whole reason a healthy boot reports `Tainted: G W` and
`tainted=512`, and together they are about 60 lines — the largest single
block of noise in the log.

**What happens.** `dsi_phy_driver_probe()` finishes by calling
`devm_of_clk_add_hw_provider()`, which makes the PHY's PLL outputs
resolvable and so triggers `clk_core_reparent_orphans_nolock()`. For an
orphan that already carries a prepare count, that path calls
`__clk_set_parent_before()` → `clk_core_prepare_enable(parent)`, which
walks up into `dsi_pll_7nm_vco_prepare()`. At probe the PHY has not been
enabled and the VCO has never been given a rate, so starting the PLL and
polling `COMMON_STATUS_ONE` times out after 5 ms — that is the "lock
failed" pair. `clk_core_prepare_enable()` returns the error, but
`__clk_set_parent_before()` is `void` and discards it, so the matching
`__clk_set_parent_after()` goes ahead and calls `clk_core_disable_lock()`
and `clk_core_unprepare()` on a clock whose counts the failed prepare
already rolled back to zero. Hence "already disabled" and "already
unprepared".

**Why it is left alone.** Display works *because* the prepare fails and
unwinds. The obvious-looking local fix — have `dsi_pll_7nm_vco_prepare()`
return 0 early when the VCO has no rate yet — would leave
`prepare_count` at 1, which makes the later real `clk_prepare()` from
display init a no-op and would take the panel down. The correct fix is in
the clk core: propagate the error out of the orphan-reparent path instead
of discarding it. That is a change to code every clock on every platform
goes through, to silence two cosmetic warnings on one board. Not a trade
worth making here.

**If it ever needs doing:** the discarded return value is in
`__clk_set_parent_before()` in `drivers/clk/clk.c`, and the single caller
that cannot handle a failure is `clk_core_reparent_orphans_nolock()`.

---

## NOISE-2 — three error-level messages on healthy paths were demoted

**Patches 0016, 0017, 0018.** Each is a `dev_err` → `dev_dbg` demotion on
a code path that is reached during normal operation, with a comment
explaining why the handler is empty or the condition expected. None of
them changes behaviour; all three are kebab-independent and upstreamable.

| Patch | Message | Why it is not an error |
|---|---|---|
| 0016 | `qcom,pmic-typec ...: isr: tx_sig` | The PHY finished putting a hard or cable reset on the wire — a sequence TCPM asked for and whose completion it tracks itself. The handler does nothing else. A partner needing several resets filled the log with error lines saying the hardware obeyed. |
| 0017 | `wcd938x_codec audio-codec: Impedance detect ramp error, c1=0, x1=0x0` | The ZDET ramp returns nothing when there is no load across HPHL/HPHR — the normal state unplugged, and the permanent state on this board, which has no 3.5 mm jack at all. |
| 0018 | `qcom_q6v5_pas ...: Handover signaled, but it already happened` | See MODEM-3. |

**Verified on hardware, kernel `#23`:** all three are at zero
occurrences, and the `tfa2 ... hw_params` line (also demoted) is at zero
too. A normalised diff of every error/warning/timeout-shaped line against
the r21 boot shows **no new shapes in r23** — the demotions removed lines
and introduced none.

**Headline effect of r22 on the boot log:** 3762 lines → **1251 lines**,
a 67% reduction, with distinct problem-shaped messages down from 36 to
25.

**One honest correction on the Sahara change** (MODEM-2): aggregating the
per-image lines into a per-session summary barely helped — 23 Sahara
lines before, 20 after. The modem does not run one Sahara session for 15
images; it runs about 14 sessions of one image each, so "one line per
session" and "one line per image" are nearly the same thing. The line is
better (it carries a byte count) but the count is not meaningfully lower,
and the win claimed for it before the measurement was overstated.

**0017 is worth understanding, because the trigger is not obvious.**
`HPHL Impedance`, `HPHR Impedance` and `HPH Type` are ordinary *readable*
ALSA controls whose get handlers call `wcd_mbhc_get_impedance()`, so each
read runs a fresh ZDET ramp. Anything that enumerates the card's controls
— `alsactl store`, PipeWire, `amixer contents` — therefore trips the
failure path several times at every boot. That is why there were six
identical lines and not one. Demoting is the right answer rather than a
userspace rule, because there is no clean way to tell userspace not to
read a control that advertises itself as readable.

---

## NOISE-3 — resolved: MHI chatter comes from a modprobe option, and is deliberate

**Status:** root cause found 2026-10-01. **Kept on purpose**, now
documented and packaged.

The MHI lines are all `dev_dbg` in `drivers/bus/mhi/host/*.c`, so
something had to be enabling them. It was a single hand-placed file from
the original bring-up:

```
# /etc/modprobe.d/mhi.conf
options mhi dyndbg=+p
```

That turns on every `dev_dbg` site in the `mhi` module — 46 of them, out
of 126 enabled sites on the whole system.

**I was wrong about `CONFIG_MHI_BUS_DEBUG`** and should correct it here,
because the wrong guess is the kind that wastes a rebuild: its Makefile
entry is `mhi-$(CONFIG_MHI_BUS_DEBUG) += debugfs.o` and nothing else. It
adds the debugfs interface and enables no prints at all. It stays `=y`.

**Why it is kept.** It costs about 35 lines per boot, and those lines are
the SDX55 boot trace and the QCA6390 runtime-PM transitions:

```
mhi mhi0: Requested to power ON
mhi mhi0: Received EE event: SECONDARY BOOTLOADER
mhi mhi0: Received EE event: MISSION MODE
mhi mhi0: Power on setup success
mhi mhi1: State change event to state: M0
```

The modem is still being brought up, and this is the first place anyone
looks when it does not reach mission mode. Nothing in the `mhi` driver
that reports an actual failure is a `dev_dbg`, so turning it off loses
diagnostics and hides no errors.

**What changed:** the policy is no longer an undocumented file in `/etc`.
`device/oneplus-kebab/90-kebab-mhi-debug.conf` now ships it to
`/usr/lib/modprobe.d/` with a comment saying what it costs, why it is on,
and how to turn it off:

```bash
echo 'module mhi -p' | sudo tee /sys/kernel/debug/dynamic_debug/control
```

**Loose end:** `/etc/modprobe.d/mhi.conf` still exists on the device and
takes precedence over the packaged copy. The two say the same thing, so
behaviour is identical; remove the `/etc` one so the commented version
governs. There is a second bring-up leftover in the same directory:
`/etc/modprobe.d/kebab-audio-ab.conf`, containing `blacklist
snd_soc_tfa9872`. That one is now **redundant** — AUDIO-1 was fixed
properly by dropping `CONFIG_SND_SOC_TFA9872` from the kernel config, so
the module is not built and there is nothing to blacklist. It is harmless
but can go.

---

## NOISE-5 — the 221 `Fixed dependency cycle(s)` lines are the largest remaining block

**Status:** measured, understood, **not fixable here.**

After r22 this is by far the biggest single contributor to the boot log —
221 of 1251 lines, about 18%:

```
spmi@c440000/pmic@2/typec@1500/connector: Fixed dependency cycle(s) with usb@a6f8800/usb@a600000
display-subsystem@ae00000/dsi@ae94000: Fixed dependency cycle(s) with display-subsystem@ae00000/dsi@ae94000/panel@0
cci@ac50000/i2c-bus@1/camera@10: Fixed dependency cycle(s) with camss@ac6a000
tpda@6004000: Fixed dependency cycle(s) with funnel@6005000
...
```

It is `pr_info()` from `drivers/base/core.c:2209`, emitted by fw_devlink
when it breaks a cycle it found in the devicetree. 37 distinct nodes are
involved.

**Why the cycles are real and legitimate.** The bindings these nodes use
are deliberately bidirectional: a panel references its DSI host and the
DSI host references its panel; a Type-C connector references the USB
controller, the orientation mux and the PHY, and they reference it back;
a camera sensor and CAMSS reference each other. fw_devlink finds a cycle,
reports it, relaxes it, and probe ordering then works out correctly. The
message is a description of normal operation on any SoC using these
bindings, not a kebab defect.

**What I considered and rejected:**

* **`status = "disabled"` on the coresight nodes.** fw_devlink does skip
  unavailable nodes (`of_device_is_available()` in
  `drivers/of/property.c`), so this would work — but coresight
  (`funnel@`, `etm@`, `tpdm@`, `tpda@`, `stm@`, `replicator@`) is only
  about a third of the lines. The rest come from display, Type-C, camera
  and UFS — hardware this board actually uses, which cannot be disabled.
  Thirty-odd lines of devicetree to remove a third of one `pr_info`
  class was not worth it.
* **`fw_devlink=permissive` or `=off` on the kernel command line.** This
  would silence all of it and is tempting precisely because it is
  configuration rather than code. Rejected: it changes probe-ordering
  semantics for every driver on the system, and this board depends on
  deferred probe behaving correctly — patch `0005` exists specifically so
  the DP altmode returns `-EPROBE_DEFER` and is retried. Trading probe
  correctness for a quieter log is the wrong direction.

If these ever need to go, the honest fix is upstream: `pr_info` →
`pr_debug` in `fw_devlink_relax_cycle()`, since a cycle that fw_devlink
successfully relaxed is not news.

---

## NOISE-4 — noise deliberately left in place

Each of these is one or two lines per boot and each one says something
true that is worth keeping:

* `ramoops: error in header` — the pstore region has no valid record yet,
  which is the expected state when the previous boot did not crash. See
  RAMOOPS-1.
* `a650_sqe.fw ... failed with error -2` followed by a successful load —
  the firmware loader tries the old path before the new one. Upstream
  tries both on purpose.
* `qnoc-sm8250 ...: sync_state() pending due to 1dfa000.crypto`,
  `gcc-sm8250 ...: sync_state() pending due to 3d6a000.gmu` (×4) — the
  driver core reporting that it cannot finalise provider state while a
  consumer is still unprobed. They resolve.
* `MultiMedia1: ASoC: no backend DAIs enabled for MultiMedia1, possibly
  missing ALSA mixer-based routing or UCM profile` — `dev_err_once`, so it
  is one line ever. It fires when something opens the frontend before the
  UCM Speaker sequence has set `TERT_MI2S_RX Audio Mixer MultiMedia1`, and
  the message is an accurate description of that moment.
* `qcom-soundwire 3230000.soundwire: qcom_swrm_irq_handler: SWR read
  enable valid mismatch` — `dev_err_ratelimited`, and the handler masks
  the interrupt after the first occurrence, so it is self-limiting. It
  does report a real bus condition on swr2 (TX), which is worth knowing.
* `rfkill: input handler disabled`, `gnss: GNSS driver registered with
  major 511`, `sync_state() pending` — ordinary informational lines.

The `tfa2 ...: hw_params: ...` line was *not* left in place: it fired on
every stream start (26 times in one session) and is now `dev_dbg`. The
per-amp `tfa9874 rev 0xc74 ready (channel=N, ...)` line is kept — it is
one line per amp at probe and it is now the quickest way to confirm the
stereo fix of AUDIO-3 took effect.

---

## CAMERA-1 — the diagnostic and revert patches are gone

**Removed:** `0060`, `0061`, `0064`, `0066`, `0068`, `0069`, `0070`,
`0071`, `0073`, `0074`, `0075`, `0076`, `0078`, `0079`, `0081`, `0084`,
`0087`, `0089`, `0090`.

`0060`/`0077`, `0061`/`0063` and `0066`/`0080` are superseded pairs; the
rest are temporary bus scans, diagnostics and their reverts. `0078` set
400 kHz on `cci1_i2c0`, which became a no-op once `0083` moved the sensor
to `cci1_i2c1`.

**What survives, and where:** the six real root causes.

| Finding | Where it is now |
|---|---|
| VANA gpio156 is active-**high** | dts, `vreg_cam_front_vana` |
| `cam_vdig` must be 1104000 µV (8 mV step) | dts, `pm8008_l1` |
| Vendor rail sequence VANA→VDIG→VIO→MCLK→RESET | series patch `0009` |
| Front camera is on CCI1 **master 1** | dts, `&cci1_i2c1` |
| Slave address is `0x10`, not `0x1a` | dts, `camera@10` |
| CAMSS needs vdda supplies and 0-based `data-lanes` + `clock-lanes = <7>` | dts, `&camss` |

**Note on `0069`:** the "powered reset pulse" diagnostic is not gone, it is
absorbed. Series patch `0009` asserts reset before the rails come up and
releases it after MCLK, which subsumes what `0069` was probing for.

**Risk:** low. Every dropped patch is either a diagnostic, a revert, or the
earlier half of a superseded pair, and the final devicetree state was
verified identical.

---

## CAMERA-2 — the IMX471 driver is now two commits, with a verbatim upstream base

**Was:** patch `0058` backported the driver with an `of_match_table` folded
in; patch `0082` then added the rails.

**Now:** series patch `0008` is byte-identical to current mainline
`drivers/media/i2c/imx471.c` (verified by download and `diff`), and series
patch `0009` adds both the OF match and the three-rail sequencing.

**Why:** the backport can be refreshed with a plain file copy on the next
kernel bump, and the board delta stays reviewable on its own.

**Still needed upstream:** mainline imx471 still declares only `vana`
(checked against `torvalds/linux` master on 2026-09-30), so patch `0009`
cannot be dropped by rebasing.

---

## RAMOOPS-1 — pstore stays enabled

**Kept:** the `ramoops@b0000000` reserved-memory node, 4 MiB, 2 MiB console
record, ECC on.

**Why, even though the brief called it optional:** this refactor is
expected to produce regressions, and several of the findings behind these
patches were only diagnosable because the previous boot's console survived
a silent SoC reset. A 4 MiB carve-out is a good trade while that is true.

**To remove:** delete the `ramoops@b0000000` node from the
`reserved-memory` block. `CONFIG_PSTORE_RAM` can stay on; without the node
nothing registers.

---

## BUILD-1 — closed: the real Clang build is clean

**Status:** closed 2026-10-01 on the Arch builder.

Development verification used `aarch64-linux-gnu-gcc` at `W=1`, because no
Clang toolchain was available there. `kernel/APKBUILD` builds with
`LLVM=1`, so that left the first real Clang build unproven.

It has now run: `pmbootstrap build linux-postmarketos-qcom-sm8250 --force`
produced `linux-postmarketos-qcom-sm8250-7.2.0-r19.apk` with **zero
compiler warnings**. All 29 warnings in the log are `apk`/`abuild` noise —
missing x86_64 `APKINDEX` files and pre-existing stray files in the aport
directory (`APKBUILD.bak-00xx`, a leftover `.bak-cleanup` patch, a stray
`.py`), none of which are in `source=`.

All 13 patches applied with no fuzz or offset, and `sha512sums` verified.

**Devicetree chain of custody is complete.** The DTB inside the built apk
is byte-identical to the one compared against the original 81-patch stack
during development:

```
493bcdf4ff16ae9b52a7be0476eee6454e1b2251edf3afd1f3579b103c0d106d
```

So the devicetree that will actually boot is exactly the one proven to
differ from the bring-up stack only in DT-1..DT-5.

The other packages built too: `device-oneplus-kebab-7-r0` with both
subpackages (`-audio`, `-modem`), `kebab-modem-tools-1.1-r0` and
`tqftpserv-sdx55-1.0-r1`. Package contents were listed and contain exactly
what the APKBUILDs promise, with no file owned by two packages.

---

## BUILD-2 — abuild rejects `local@invalid` as a maintainer address

**Status:** fixed 2026-10-01.

The handoff archive sanitised patch authorship to
`kebab bring-up <local@invalid>`, and that string was carried into
`packages/kebab-modem-tools/APKBUILD` as the maintainer. `abuild` refuses
it:

```
>>> ERROR: kebab-modem-tools: 'kebab bring-up <local@invalid>' is not a
           valid rfc822 address
>>> ERROR: kebab-modem-tools: Provide a valid RFC822 maintainer address
```

That aborted the whole `pmbootstrap build` invocation, which also stopped
`device-oneplus-kebab` from being built in the same run — so the symptom
was two packages missing, not one.

Now `kebab bring-up <kebab@example.com>`. `example.com` is reserved by
RFC 2606 and `abuild` accepts it. Note this only affects APKBUILD
`maintainer=`/`# Maintainer:` lines; `local@invalid` inside patch mail
headers is fine and is left alone.

---

## PACKAGING-2 — the module sync must replace the tree, not overlay it

**Status:** found and fixed on the device, 2026-10-01.

`kebab-build.sh` synced `/lib/modules` by piping a tar of the builder's
chroot tree to the phone and extracting it. `tar x` only adds and
overwrites: a module the new kernel no longer builds stays on disk.

That is not a cosmetic problem here. Dropping the second TFA9874 driver
(AUDIO-1) left `snd-soc-tfa9872.ko.zst` behind, and it matches the same
`nxp,tfa9874` devicetree compatible as the driver that replaced it — so it
could still be autoloaded and bind the amplifiers instead of `tfa2`,
silently restoring the exact ambiguity the refactor removed.

The script's own post-sync hash comparison caught it:

```
MODULE TREES DIFFER: builder 97feb7637205589b, phone b1927fdfaf1d7882
```

583 modules on the builder, 584 on the phone, every common file identical,
one extra file. The sync now moves the old tree to
`/lib/modules/$KVER.replaced`, extracts fresh, runs `depmod`, and only
then deletes the old one — so a failed transfer is still recoverable.

After the fix both sides hash `97feb7637205589b`.

**Keep that verification step.** It is the only thing standing between a
dropped driver and a device that quietly keeps using it.

---

## PACKAGING-3 — the old modules hard-fail on the new kernel, which is good

**Status:** observed 2026-10-01, no action needed.

Both kernels report release `7.2.0`, so `/lib/modules/7.2.0` is the same
directory and vermagic matches (`7.2.0 SMP preempt mod_unload aarch64`).
The journal assumed that meant September's modules would silently load
against the October kernel. They do not:

```
modprobe: ERROR: could not insert 'bluetooth': Invalid argument
failed to validate module [rfkill] BTF: -22
```

`CONFIG_DEBUG_INFO_BTF_MODULES` validates each module's BTF against the
kernel's, and a mismatch is fatal (`-EINVAL`). On the first `#20` boot
only 8 of 584 modules loaded. After the sync, 85-86 load normally.

This is a safety property worth keeping: a forgotten module sync fails
loudly instead of producing a half-stale system that is impossible to
reason about. It does mean the window between flashing and syncing has no
Wi-Fi, no camera, no audio and no modem — expected, not a fault.

---

## PACKAGING-4 — apk will replace the custom kernel unless its database is made truthful

**Status:** fixed 2026-10-01 by `scripts/device-pin-kernel.sh`.

The device was flashed by fastboot and its modules pushed as a tarball, so
nothing ever told `apk` about the local kernel. Its database still said:

```
installed:        linux-postmarketos-qcom-sm8250-7.2.0-r0     (the pmOS repo build)
/etc/apk/world:   linux-postmarketos-qcom-sm8250=7.2.0-r0
running:          #22-postmarketos-qcom-sm8250                (our build)
```

**Pinning alone does not help, and that is the trap.** The pin above was
already in place. It stops apk changing the *version*, but apk owns the
*files*:

```
apk info -L linux-postmarketos-qcom-sm8250 | grep -c usr/lib/modules  -> 584
apk info --who-owns /boot/vmlinuz  -> linux-postmarketos-qcom-sm8250-7.2.0-r0
```

So `apk fix`, `apk upgrade` once upstream bumps the pkgrel, or any
operation that pulls the kernel as a dependency, will restore r0's modules
over the running kernel. The symptom is not an error — it is Wi-Fi, audio,
camera and the modem disappearing after the next reboot, because the
modules no longer match the kernel's BTF. That is exactly the failure seen
on the first boot after the very first flash (PACKAGING-3).

`/boot/vmlinuz` was still the 30 August repo build, and `/boot/boot.img`
had been regenerated from it, so a reflash *from the rootfs* would have
installed the wrong kernel.

### The fix

1. trust pmbootstrap's local signing key (`pmos@local-*.rsa.pub`) on the
   device, so locally built packages verify normally;
2. `apk add` the locally built `.apk`, which makes the database truthful,
   lands every file root-owned, and regenerates `/boot/initramfs`,
   `/boot/sm8250-oneplus-kebab.dtb` and `/boot/boot.img` from *our*
   vmlinuz;
3. pin that exact version in `/etc/apk/world`.

Verified after: `/boot/vmlinuz` hashes `153e9fa0dbea611c…`, byte-identical
to the builder's r21 apk, and both `apk upgrade --simulate` and
`apk fix --simulate` leave the kernel alone.

This also **replaces the module tarball sync**, which is why
`kebab-build.sh` now deploys by apk. `--modules-tar` keeps the old path for
when apk on the device is broken or offline.

### apk-tools 3 detail worth knowing

apk 3 keeps two kinds of constraint and writes the second one itself
whenever a package is installed from a local file:

```
linux-postmarketos-qcom-sm8250=7.2.0-r21
linux-postmarketos-qcom-sm8250><Q1tEiOkgccGKFHubXAgdSHKDPOdgU=
```

The second is a **content hash**, not a version. Tested individually, apk
is satisfied by either one alone. Both are kept: apk re-adds its hash line
on every `apk add ./file.apk`, so removing it is pointless, and the
readable `=version` line is the one that unambiguously blocks a move to a
repo build. A `sed` that matches only `pkg=` leaves the hash line behind
and the entries accumulate — the scripts match every operator apk can
emit (`[=<>!~]`).

### What is still only userland

Worth keeping in proportion: apk cannot touch the boot partition. `/boot`
on this device is `loop0p1` **inside the rootfs image**, and the device
boots the separately flashed `boot_a`. So even a worst-case apk run
replaces modules and rootfs `/boot` staging — recoverable by re-running
`device-pin-kernel.sh` — and cannot make the device unbootable.

---

## PACKAGING-5 — `50-sm8250-rates.conf` exists only on the device, not in the tree

**Status:** open. Low risk today, but it is an undocumented dependency.

The tfa2 driver's own comment names a WirePlumber rule as the fix for an
earlier "faint playback" theory:

> the actual cause was a userspace format/buffering mismatch, fixed by the
> WirePlumber ALSA rate rule (50-sm8250-rates.conf, forces
> S16LE/48000/period 1024)

That file is **not** in this tree and not on the build host. It exists
only on the phone, installed by hand during the original bring-up, and
the only trace of it anywhere in the refactor is that comment (and the
same comment in `original/kernel-aport/0043-kebab-tfa2-codec-driver.patch`).
A clean install from these packages would not have it.

**Why it probably no longer matters for correctness:** the reason the rate
rule was load-bearing was that `tfa2_tdm_setup()` programmed a fixed
16-bit geometry at probe. `tfa2_hw_params()` now derives `FSBCLKS`,
`NSLOTS`, `SLOTBITS` and `SWIDTH` from whatever the backend negotiated,
so `S24_LE` (64 bclk, `FSBCLKS=2`) and `S16_LE` (32 bclk, `FSBCLKS=0`)
are both handled, and slot 1 is the right channel in either case. The
rule is a known-good pin, not a requirement.

**To close this:** retrieve it from the device and add it to the
`device-oneplus-kebab-audio` subpackage alongside the UCM files —

```bash
scp jeff@172.16.42.1:/usr/share/wireplumber/wireplumber.conf.d/50-sm8250-rates.conf \
    device/oneplus-kebab/
```

(the path may instead be under `/etc/wireplumber/`; check both). Then
decide deliberately whether to keep it: if it is kept it should say in a
comment that it is a pin and not a workaround, now that the driver no
longer depends on it.

---

## BUILD-3 — `sha512sums` is matched to `source=` by position, not by name

**Found the hard way on 2026-10-01**, while adding one file to
`device/oneplus-kebab`.

Appending a new entry to the end of the `sha512sums=` block, with the
matching entry inserted mid-list in `source=`, fails the build:

```
/home/pmos/build/77-mm-sdx55-fusion.rules: OK
/home/pmos/build/90-kebab-mhi-debug.conf: FAILED
sha512sum: WARNING: 1 of 1 computed checksums did NOT match
>>> ERROR: device-oneplus-kebab: Use 'abuild checksum' to generate/update the checksum(s)
```

The hash in the file was **correct** — identical on the workstation, the
build host and in pmaports. What was wrong was its *position*: abuild
pairs the two lists positionally, so a sum at index 12 was checked
against the source at index 12. `pmbootstrap checksum` "fixed" it by
doing nothing except moving that one line from 135 to 130.

The error message is actively misleading here, because the advice it
gives ("use abuild checksum") is right while the diagnosis it implies
(the file changed) is wrong. Worth remembering: a `FAILED` line for a
file whose hash you have just verified by hand means the *order* is
wrong, not the content.

**Rule when editing either APKBUILD by hand:** regenerate the whole
`sha512sums` block in `source=` order rather than appending. Both
APKBUILDs in this tree are now in order, and the check is one line:

```python
src  = re.search(r'source="\n(.*?)"\n', s, re.S).group(1).split()
sums = [l.split()[1] for l in re.search(r'sha512sums="\n(.*?)"\n', s, re.S).group(1).strip().split('\n')]
assert [x.split('::')[0] for x in src] == sums
```

(For `kernel/APKBUILD` the first three `source=` entries are `$pkgname-$_tag.tar.gz::URL`,
`$_config` and `$_kebab_dts`, so compare after expansion — the r22 kernel
build was only correct by luck, because the three new patches happened to
be appended at the end of *both* lists.)

**Also fixed in `install-to-pmaports.sh`:** it now deletes `*.dtb` left in
either aport directory by a previous abuild. Those produce
`WARNING: ... is not in $source/$install/$triggers` on every build.

---

## PACKAGING-1 — `mhi_efs_sync` is now actually built

**Was:** `mhi-efs-sync.service` started `/usr/bin/mhi_efs_sync`, but no
package built it; the binary was installed by hand and only its GPL source
was archived for reference.

**Now:** `packages/kebab-modem-tools` compiles it, and owns its unit. The
source is byte-identical to the archived copy (sha512
`61b09274…`, matching `original/userspace/reference/mhi-efs-sync/BUILD.md`).

**Unit ownership was untangled at the same time,** because two packages
would otherwise have installed the same files:

| Unit | Package |
|---|---|
| `pm-service-native.service` | `kebab-modem-tools` |
| `mhi-efs-sync.service` | `kebab-modem-tools` |
| `tqftpserv-sdx55.service` | `tqftpserv-sdx55` |
| `kebab-uim-provision.service` | `device-oneplus-kebab-modem` |

**Verified 2026-10-01:** `mhi_efs_sync.c` compiles under musl with Alpine's
headers. `kebab-modem-tools-1.1-r0.apk` ships
`/usr/bin/mhi_efs_sync` alongside `pm_service_native` and `diag_reader`,
and `/usr/lib/systemd/system/mhi-efs-sync.service`.

---

## NOT CARRIED — patches that were already inactive

These were in `original/kernel-aport/` but not in the working `source=`
list, and are not carried forward. Listed so nobody re-derives them.

| Patch | Why not |
|---|---|
| `0011-mhi-sdx55-qcom-profile` | superseded by the Fusion profile (series `0011`) |
| `0015-kebab-displayport-altmode-usb-c` | put the FSA4480 on `&i2c1`, the NFC bus; superseded by `0018` on `&i2c15` |
| `0025-kebab-usb-hsphy-tuning` | stock HS PHY tuning values, never enabled, never evaluated. If USB hubs show EPROTO control-transfer failures, this is the first thing to try |
| `0035-kebab-otg-vbus-ramp-delay` | reinstated in the dts at 1.2 s instead of 300 ms — see USB-1 |
| `0040-kebab-otg-ilim-3a` | superseded by the devicetree current constraint — see USB-2 |
