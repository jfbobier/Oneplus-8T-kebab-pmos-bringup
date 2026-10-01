# kebab (OnePlus 8T, SM8250 + SDX55) postmarketOS — RESEARCH

**Status as of 2026-09-21:**
- **FRONT CAMERA WORKING** (unchanged since 09-18)
- **MODEM BOOTS TO MISSION MODE AND STAYS UP** (new today). Factory NV restored
  and persisting. Radio still will not go online — blocker identified as Policy
  Manager reporting `Num_Techs 0`, not calibration.
- **GPS: not working, and it is the same blocker as the radio** — do not treat
  it as separate work.

Canonical entry point. Supersedes `kebab-handoff-MASTER-2026-09-18.md` (whose
modem status — "reaches SECONDARY BOOTLOADER" — is badly stale) and
`kebab-handoff-MASTER-2026-09-17.md`.

**The modem work has its own document: `MODEM.md`.**
It is long and detailed; this file carries only the summary and the shared
infrastructure. Read the modem doc before touching anything modem-related.


---

## 1. Device / environment

| | |
|---|---|
| Phone | OnePlus 8T, codename **kebab**, model KB2003, SM8250 + SDX55 |
| Project id | 19805 (`androidboot.prjname`), `instantnoodle` codename in cmdline |




---

## 2. Front camera (IMX471) — WORKING

Probe, identify, media graph, CSI reception, frames, GNOME camera app. Verified
live:

```
entity 329: imx471 24-0010, /dev/v4l-subdev24
  pad0 SOURCE fmt:SRGGB10_1X10/1928x1088 crop.bounds:(8,8)/4656x3496
    -> "msm_csiphy4":0 [ENABLED,IMMUTABLE]

imx471 -> csiphy4 -> csid0 -> vfe0_rdi0 -> /dev/video0
```

Read back over I2C while streaming: `0x0016 = 0x0471` (chip id),
`0x0100 = 0x01` (streaming), `0x0114 = 0x03` (4-lane), `0x0112 = 0x0a0a`
(RAW10). Final CSIPHY state: `lane_mask=0x8f`, data lanes at pos 0–3, clk lane
pos 7, `common_status0=0x00000004`.

The 09-17 conclusion — "most likely a physical hardware fault" — was wrong. The
hardware was always fine. **Six independent software bugs were stacked, and
several corrupted the evidence used to diagnose the others.**

### 2.1 The six root causes

| # | Patch | Bug |
|---|---|---|
| 1 | 0077 | VANA load-switch on gpio156 declared `GPIO_ACTIVE_LOW`; it is active-**high**. Rail was cut during every chip-id read. |
| 2 | 0080 | `cam_vdig` (pm8008_l1) parked at 1.2 V, 100 mV over vendor spec. |
| 3 | 0082 | Driver managed only `vana`; `vio`/`vdig` were `regulator-always-on`, so the vendor power sequence never ran and the die never cold-started. |
| 4 | 0083 | Sensor node on CCI1 **master 0**; the vendor puts the front camera on **master 1**. Master 0 is the GC5035 macro camera. |
| 5 | 0085 | Slave address `0x1a` (family default, never verified). Actual address is **`0x10`**. |
| 6 | 0086 + 0088 | camss had no `vdda-phy`/`vdda-pll` supplies, and the CSIPHY endpoint used sensor-side 1-based `data-lanes` with no `clock-lanes`, so physical lane 0 was never enabled. |

### 2.2 VANA polarity (0077)

`kona-oem-camera-kebab_t0.dtsi:635` and `kebab-19805-camera.dts:14032` both set
gpio156 `output-low; bias-pull-down;` in the **suspend** state. Off is low, so on
is high. With `GPIO_ACTIVE_LOW`, gpiolib parked the pin high (VANA on) while the
regulator was nominally disabled, and drove it low (VANA **off**) exactly when
`imx471_power_on()` enabled the supply.

### 2.3 Vendor power sequence (0082)

Recovered by parsing
`odm/lib64/camera/com.qti.sensormodule.truly_imx471.bin`, a QTI Chromatix
"Parameter Parser V2.0.0" container. Descriptor table: 32-byte name + 5×u64 at
stride `0x48` from `0xf0`; data base `0x142f0`;
`offset(n+1) = offset(n) + size(n)`. Two 120-byte `powerSetting` arrays, each
five 24-byte `{seq_type, config_val, delay}` records, seq_type per
`enum msm_camera_power_seq_type` (MCLK=0, VANA=1, VDIG=2, VIO=3, RESET=8):

```
powerSetting[0] up    VANA(1,1) VDIG(1,1) VIO(1,1) MCLK(19200000,1) RESET(1,1)
powerSetting[1] down  RESET(0,1) MCLK(0,1) VIO(0,1) VDIG(0,1) VANA(0,1)
```

The MCLK record's `config_val` is `0x0124f800` = 19200000, matching
`clock-rates` in the vendor DT — that is the checksum confirming the layout
decode. **VANA must lead.**

### 2.4 pm8008 LDO step size (0080)

`1100000uV` is **not settable**: the pm8008 LDO steps in 8 mV, so the core
rounds min up to 1104000 and max down to 1096000, inverts the window and
refuses:

```
pm8008_l1: unsupportable voltage constraints 1104000-1096000uV
qcom-pm8008-regulator: failed to register regulator ldo1: -22
```

Because ldo1 failing aborts the whole driver probe, **L1 and L4 both vanish** —
taking cam_vio down with cam_vdig and unpowering the entire camera bus. Use
`1104000`.

### 2.5 CSIPHY lane numbering (0088) — the final blocker

The camss-side endpoint had the sensor's 1-based numbering and no `clock-lanes`.
`csiphy_lanes_enable()` builds CTRL5 as `BIT(pos * 2)`:

```
positions 1,2,3,4 -> BIT(2,4,6,8) = 0x154   (wrong, lane 0 never enabled)
positions 0,1,2,3 -> BIT(0,2,4,6) = 0x55    (correct)
```

`qcom,sm8250-camss.yaml` lists `clock-lanes` **and** `data-lanes` under
`required`. Correct form, matching `sm8250-xiaomi-elish-common.dtsi`:

```dts
csiphy4_front_ep: endpoint {
	clock-lanes = <7>;
	data-lanes = <0 1 2 3>;
	bus-type = <MEDIA_BUS_TYPE_CSI2_DPHY>;
	remote-endpoint = <&camf_imx471_ep>;
};
```

The **sensor** endpoint keeps `data-lanes = <1 2 3 4>` — that side is genuinely
1-based and `imx471_check_hwcfg()` requires exactly 4 lanes.

### 2.6 Why the 09-17 camera diagnosis went wrong

Worth reading before trusting any negative result in the old docs. The VANA
inversion silently invalidated four separate experiments, each of which then
became evidence for the (incorrect) hardware conclusion:

| Evidence | Why it was void |
|---|---|
| 0070 full-bus scan | Wrong CCI clock **and** VANA inverted → sensor unpowered |
| 0075 full-bus scan (called "the single most decisive test") | Clock fixed, VANA still inverted → sensor unpowered |
| 0066/0068 VDIG result (`-ENXIO`→`-ETIMEDOUT`, logged as a sensor regression) | Not the sensor. The pm8008 probe cascade above |
| 0073/0074 CCI master test | Ran before 0077 → unpowered, so its NACK meant nothing |

**The load-bearing error.** The EEPROM at `0x50`/`0x58` that every session cited
as "the bus is electrically alive, so the silent device must be a dead sensor"
belongs to the **GC5035 macro camera** on CCI1 master 0. It was never the front
module's EEPROM. The IMX471's own EEPROM is at `0x54`/`0x5c` on master 1 — a bus
the sensor node was never on. The argument that `imx471_p24c64e` implies a 24C64
part in `0x50-0x57` did not discriminate, because `0x54` is equally in range.

**Method lesson:** a userspace `i2cdetect` cannot see these sensors. Outside the
driver's power window the regulators are disabled. Address discovery must happen
inside `imx471_power_on()`.

### 2.7 Reproducing a capture by hand

```sh
M=/dev/media0
for E in '"imx471 24-0010":0' '"msm_csiphy4":0' '"msm_csiphy4":1' \
         '"msm_csid0":0' '"msm_csid0":1' '"msm_vfe0_rdi0":0' '"msm_vfe0_rdi0":1'; do
  media-ctl -d $M -V "$E [fmt:SRGGB10_1X10/1928x1088]"
done
v4l2-ctl -d /dev/video0 --set-fmt-video=width=1928,height=1088,pixelformat=pRAA
v4l2-ctl -d /dev/video0 --stream-mmap=4 --stream-count=10 --stream-to=/tmp/f.raw
```

Sensor exposes: `exposure` (1–1290), `analogue_gain` (0–800),
`vertical_blanking` (220–64447), `horizontal_blanking` (400, RO), `hflip`,
`vflip`, `camera_orientation` (Front, RO), `link_frequency` (200000000, RO).
Single mode: 1928x1088 SRGGB10_1X10.

---

## 3. Modem (SDX55) — boots and stays up; radio not online

**Full detail: `kebab-handoff-MODEM-2026-09-21.md`.** Summary only here.

### 3.1 What works

The whole flashless boot chain, end to end:

```
PRIMARY BOOTLOADER -> BHI loads sdx55m/sbl1.mbn
SECONDARY BOOTLOADER (~9 s) -> Sahara v2 on MHI ch 2/3, 15 images, all OK
MISSION MODE (~15-18 s) -> DIAG, EFS, MBIM, QMI, IPCR, IP_SW0, IP_HW0
  -> /dev/wwan0{qcdm0,efs0,mbim0,qmi0}, net mhi_swip0 / mhi_hwip0
  -> ModemManager creates a modem on QRTR node 3
SIM detected, PIN accepted, UIM 'ready' on slot 1
```

Stable indefinitely (soaked well past 1000 s with zero faults). Previously it
ERRFATAL'd at ~706 s, every time.

### 3.2 The two fixes that got it there

- **Patch 0091** — `.no_m3 = true` on the SDX55 fusion profile, plus guarding the
  `pm_runtime_allow()` in `mhi_pci_status_cb()` that silently re-armed
  autosuspend on mission mode. Without the second half, any userspace attempt to
  pin `power/control` is undone a few seconds after probe.
- **Patch 0092** — bind the EFS channel in `mhi_wwan_ctrl` via a new
  `WWAN_PORT_EFS` type, so `/dev/wwan0efs0` exists. **Required with apollo's
  `77-mm-ignore-sdx55-efs.rules`** — without that rule ModemManager AT-probes the
  new port and the modem ERRFATALs within 2 s, which is much worse than not
  adding the port at all.

Plus userspace, all from `~/kebab/ref/apollo-packages/`: `tqftpserv-sdx55`
(QRTR instance **3**, not 1), `mhi_efs_sync`, `pm_service_native`.

**Root cause of the 706 s death:** the SDX55 is a flashless RMTEFS build and
exports its EFS over MHI channel 10 at its first EFS-Sync (~640 s). Nothing was
bound, the sync never completed, and it ERRFATAL'd a fixed ~65 s later.

### 3.3 What is still broken

`Bands: 'none'`, so the radio will not go online (`DeviceNotReady`) and there is
no GPS. Today's NV restore work got the factory calibration onto the modem and
persisting (~250 KB, byte-verified, survives reboots), and the RF stack now
initialises with **no calibration read errors**. The remaining blocker is one
level up:

```
Prio 0: {Sub 0, State Baseline, Num_Techs 0, Tech0 0, Tech0_state Invalid State }
subs 0: CA band combo ''
```

Policy Manager believes the device supports **no radio access technologies**.
Next lead is its EFS data files — `/mdb/policyman/mcc2bands.mdb` and friends —
and the absent `/policyman/device_config.xml`. See the modem doc §4.4.4.

### 3.4 Two dead ends, recorded so nobody repeats them

- **Porting `drivers/esoc`.** `ESOC_BOOT_DONE` is a no-op toward the modem (it
  only calls `esoc_clink_evt_notify(ESOC_RUN_STATE)`, AP-internal), kebab's
  `qcom,mdm0` node wires only 4 GPIOs with no wake/vddmin lines, and `esoc.h`
  depends on the downstream SSR stack mainline replaced with remoteproc.
- **`qcom,sm8250-mpss-pas` / `remoteproc@4080000`.** SM8250 has no internal
  modem; `smp2p_modem_in/out` are absent from `sm8250.dtsi` for that reason. The
  09-18 doc's "THE PLAN" pointed here and it is wrong.

---

## 4. Patch series

`0001`–`0090` as documented in the 09-18 doc (camera, display, audio, USB/DP,
charger). Live `source=` order is in the aport APKBUILD. Added today:

| Patch | Summary | Keep |
|---|---|---|
| 0091 | `no_m3` on the SDX55 fusion profile + guard `pm_runtime_allow` | **Yes — real fix** |
| 0092 | Bind the MHI EFS channel as `WWAN_PORT_EFS` | **Yes — real fix** |

Camera fixes to keep: 0077, 0080, 0082, 0083, 0085, 0086, 0088 (+ the reverts
0081, 0089, 0090). Modem fixes to keep: 0012, 0022, 0023, 0024, 0037, 0091, 0092.

**Note on 0078.** It set 400 kHz on `cci1_i2c0`. 0083 then moved the whole sensor
block to `cci1_i2c1` and carried the rate with it, so `cci1_i2c0` is back at the
1 MHz `sm8250.dtsi` default. 0078 is therefore a historical no-op. If the GC5035
on master 0 is ever brought up, it will likely want 400 kHz too.

Older deferred cleanup, unchanged: 0073/0074 and 0075/0076 are cancel-pairs
still listed in `source=`; net effect zero.

---

---

## 6. Useful references

- **`sm8250-xiaomi-elish-common.dtsi`** and `sm8250-xiaomi-pipa-common.dtsi` are
  the other SM8250 boards with camss enabled. elish independently confirms
  `vdda-phy = vreg_l5a_0p88`, `vdda-pll = vreg_l9a_1p2` and the
  `clock-lanes = <7>` / 0-based `data-lanes` form. **Check these first** for any
  future camss question — searching only `*.dts` misses them, they are `.dtsi`.
- `Documentation/devicetree/bindings/media/qcom,sm8250-camss.yaml` —
  `clock-lanes` and `data-lanes` are both `required` per port.
- `imx471.c` is genuine upstream (Intel 2025 / Kate Hsuan, Red Hat 2026), written
  for an Intel IPU laptop. It manages only `vana` by design; the board
  integration is ours (0082).
- Downstream Qualcomm camera source, GPL and readable:
  `~/kebab/recovery/kebab-kernel/techpack/camera/drivers/cam_sensor_module/`.
  `cam_sensor_utils/cam_sensor_cmn_header.h:132` has the power-seq enum.
- **`~/kebab/ref/apollo-packages/`** — Xiaomi Mi 10T, same SM8250 + SDX55. The
  reference implementation for every modem question. Its inline comments are
  unusually good and were right about every point they covered.

---
