# Sanitized technical handoff — OnePlus 8T (kebab)

> Public copy derived from the working handoff dated 2026-09-22. Local usernames, private builder details, and carrier-identifying text were removed or generalized. No firmware images, EFS/NV dumps, SIM credentials, IMEI, ICCID, or DIAG captures are included in this repository.


**Status as of 2026-09-22:**
- **FRONT CAMERA WORKING** (unchanged since 09-18)
- **MODEM BOOTS TO MISSION MODE AND STAYS UP** (since 09-21)
- **SIM AND PIN FULLY AUTOMATIC ACROSS REBOOTS** (new today — no GNOME prompt,
  no manual step)
- **RADIO STILL OFFLINE.** Localised to one abort inside the modem's MCFG state
  machine, with the modem's own log as proof. **GPS is blocked by the same cause
  — it is not separate work.**

Canonical entry point. Supersedes `kebab-handoff-MASTER-2026-09-21.md` and
everything before it.

**Document chain — read in this order for modem work:**
1. `kebab-handoff-MODEM-2026-09-21.md` — boot chain, Sahara, MHI stability,
   patches 0091/0092, the EFS/RMTEFS root cause. Still accurate.
2. `kebab-handoff-MODEM-2026-09-22.md` — SIM/PIN bring-up and the radio blocker.
   **Supersedes the morning doc `kebab-handoff-2026-09-22.md`, whose causal
   chain is wrong** (see its §1).

- **Next free patch number: 0093.**
- pkgrel **18**. Kernel tree HEAD unchanged: `e5db13c6f`.
- No kernel patches, no flash, no rebuild since 09-21. All modem work since has
  been userspace + EFS over DIAG.

---

## 1. Device / environment

| | |
|---|---|
| Phone | OnePlus 8T, codename **kebab**, model KB2003, SM8250 + SDX55 |
| Project id | 19805 (`androidboot.prjname`), `instantnoodle` in cmdline |
| Builder | `<private-builder-host>` |
| Phone from builder | `ssh -n <user>@172.16.42.1` (postmarketOS USB networking) |
| Kernel aport | `~/.local/var/pmbootstrap/cache_git/pmaports/device/testing/linux-postmarketos-qcom-sm8250/` — **canonical patch location** |
| Kernel tree | `$KEBAB_WORKDIR/camwork/tree` (HEAD `e5db13c6f`) |
| Downstream kernel (GPL, readable) | `$KEBAB_WORKDIR/recovery/kebab-kernel/` |
| Firmware dump | `$KEBAB_WORKDIR/f13/` (modem.img mounts at `/mnt/kmodem`) |
| apollo reference port | `$KEBAB_WORKDIR/ref/apollo-packages/` — Xiaomi Mi 10T, **same SM8250 + SDX55 fusion**, the answer key for modem work |
| Rollback | `bash $KEBAB_WORKDIR/bin/kebab-rollback.sh --list` / `baseline` / `last-proven` |

**`$KEBAB_WORKDIR/patches/` is a stale partial archive** (stops at 0055). The aport
directory is the source of truth.

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
(RAW10). Final CSIPHY state: `lane_mask=0x8f`, data lanes pos 0–3, clk lane
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
| 6 | 0086 + 0088 | camss had no `vdda-phy`/`vdda-pll`, and the CSIPHY endpoint used sensor-side 1-based `data-lanes` with no `clock-lanes`, so physical lane 0 was never enabled. |

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

Read this before trusting any negative result in the old docs. The VANA
inversion silently invalidated four experiments, each of which then became
evidence for the (incorrect) hardware conclusion:

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

## 3. Modem (SDX55)

**Full detail: `kebab-handoff-MODEM-2026-09-21.md` + `-2026-09-22.md`.**
Summary only here.

### 3.1 Boot chain — works, stable

```
PRIMARY BOOTLOADER -> BHI loads sdx55m/sbl1.mbn
SECONDARY BOOTLOADER (~9 s) -> Sahara v2 on MHI ch 2/3, 15 images, all OK
MISSION MODE (~15-18 s) -> DIAG, EFS, MBIM, QMI, IPCR, IP_SW0, IP_HW0
  -> /dev/wwan0{qcdm0,efs0,mbim0,qmi0}, net mhi_swip0 / mhi_hwip0
  -> ModemManager creates a modem on QRTR node 3
```

Stable indefinitely. Previously ERRFATAL'd at ~706 s, every time.

**The two fixes:** patch **0091** (`.no_m3 = true` on the SDX55 fusion profile +
guard the `pm_runtime_allow()` in `mhi_pci_status_cb()` that silently re-armed
autosuspend on mission mode) and patch **0092** (bind the EFS channel in
`mhi_wwan_ctrl` via a new `WWAN_PORT_EFS`, so `/dev/wwan0efs0` exists).
**0092 requires apollo's `77-mm-ignore-sdx55-efs.rules`** — without it MM
AT-probes the new port and the modem ERRFATALs in 2 s, far worse than not adding
the port. Plus userspace from apollo: `tqftpserv-sdx55` (QRTR instance **3**, not
1), `mhi_efs_sync`, `pm_service_native`.

Root cause of the 706 s death: the SDX55 is a flashless RMTEFS build and exports
its EFS over MHI channel 10 at its first EFS-Sync (~640 s). Nothing was bound,
the sync never completed, and it ERRFATAL'd a fixed ~65 s later.

### 3.2 SIM and PIN — WORKING, automatic (new 2026-09-22)

Two separate bugs, both fixed:

- **A regression introduced 09-22 morning:** a service writing 9 files from the
  factory `mcfg_hw.mbn` into the modem's `/policyman/` stalled MCFG in a
  `WAIT_FOR_SESSION → SLOT_REFRESH` loop, so UIM reported the card **absent**
  while the modem could read its ICCID. Parked all 9 (EFS2 RENAME) and disabled
  `kebab-policyman-restore`. Narrowed to `device_config.xml` or
  `generic_band_restrictions.xml` — the modem regenerates the other seven.
- **Never worked on any day:** all automatic-provisioning records are absent
  (`slot_automatic_provisioning`, `subscription_provisioning`,
  `mmode/sub_config`), so the modem detected the card but never bound it to the
  Primary GW subscription. `kebab-uim-provision.service` now activates the
  session (reading the AID off the card, so a SIM swap still works) **and**
  verifies PIN1 from `/etc/kebab-sim-pin`, before ModemManager starts.

```
Card state: 'present'   Application state: 'ready'
PIN1 state: 'enabled-verified'   PIN1 retries: '3'
```

The PIN auto-verify is guarded: at most one attempt per boot, and **refuses
entirely unless retries are at the full 3**, so a wrong stored PIN cannot
PUK-lock the SIM.

### 3.3 Radio — still offline, cause localised

`--dms-get-capabilities` → `Networks: ''`. No RAT capability, so
`--dms-set-operating-mode=online` → `DeviceNotReady` (52) forever, and
`--nas-get-home-network` → `NotProvisioned` (16).

MCFG's own F3 log, captured across the boot window:

```
mcfg_utils.c       Config: <carrier-profile>
mcfg_stm_common.c  WAIT_FOR_SESSION --> MCFG_MCENTRIC_STATE_SUB_STACK_DEACT
mcfg_stm_common.c  STACK_DEACT: CMREADY --> REQUESTED
mcfg_stm_stack_deact.c  pending_exit: REQUESTED -> CMREADY
mcfg_refresh.c     MCENTRIC_STM[0] with input 1
mcfg_stm_common.c  SUB_STACK_DEACT --> MCFG_MCENTRIC_STATE_IDLE
```

Selection **works** — it resolves <carrier> by ICCID and downloads the config
in full (`rsize: 69928`, 10 blocks, all ACKed, byte-exact, served correctly by
tqftpserv). **The apply is what fails.** MCFG requests a protocol-stack
deactivation, gets it, then takes `input 1` straight to IDLE — no apply, no stack
reactivation.

**This inverts the assumed causality: the modem is offline *because MCFG took the
stack down* for a config update that then aborted**, not because it lacks a
config. `offline`, `DeviceNotReady`, `Networks: ''` and `NotProvisioned` are one
symptom.

### 3.4 Modem leads that are CLOSED — do not reopen

| Thing | Verdict |
|---|---|
| Policy manager / `Num_Techs 0` / `mcc2bands.mdb` | **Stale.** NAS now reports LTE 1–43 + extended, NR SA 41/78, NSA 1/3/7/28/41/78. The 09-21 task #14 premise is dead. |
| `offline` → SIM causal chain | Wrong. UIM provisioning is independent of DMS operating mode. |
| PIN timing into MCFG's window | Falsified. Card `ready` 3.6 s before MCFG's second pass; still `Networks: ''`. Also impossible to beat its first pass (~1 s after mission mode). |
| Stale MCFG digest records | Falsified. All 12 parked, rebooted, unchanged — and the modem regenerates them. Reverted. |
| FTM mode latched | Ruled out. NV 453 reads OK, all zeros. |
| PDC load/activate | Service 36 registered on node 3 but never answers. Irrelevant — MCFG already fetches the right config itself. |
| `rmtfs` inactive | **Expected.** `ConditionPathExists=/dev/qcom_rmtfs_mem1` can never be met; SM8250 has no internal modem. |
| tqftpserv write bug | My error, from grepping by filename. The chronological log shows the transfer completing 10/10. |
| `drivers/esoc` port | `ESOC_BOOT_DONE` is a no-op toward the modem; kebab's `qcom,mdm0` wires only 4 GPIOs; `esoc.h` needs the downstream SSR stack. |
| `qcom,sm8250-mpss-pas` / `remoteproc@4080000` | SM8250 has no internal modem; `smp2p_modem_in/out` are absent from `sm8250.dtsi` for that reason. The 09-18 doc's "THE PLAN" pointed here and is wrong. |

### 3.5 EFS tooling now proven

Over DIAG `0x4B` subsys `0x13` via `/dev/wwan0qcdm0`:

| cmd | Op | Status |
|---|---|---|
| 2/3/4/5 | OPEN/CLOSE/READ/WRITE | work (READ response swaps offset/nbytes vs the request; payload at +20) |
| **8** | UNLINK | works |
| 9 | MKDIR | **does not work** — 11 missing-directory failures from 09-21 still unresolved |
| **11/12/13** | OPENDIR/READDIR/CLOSEDIR | **work** (new 09-22) |
| **14** | RENAME | **works** — the safe, reversible way to park a file |
| 15 | STAT | works (errno at +4, size at +12) |

---

## 4. Patch series

`0001`–`0090` as documented in the 09-18 doc (camera, display, audio, USB/DP,
charger). Live `source=` order is in the aport APKBUILD.

| Patch | Summary | Keep |
|---|---|---|
| 0091 | `no_m3` on the SDX55 fusion profile + guard `pm_runtime_allow` | **Yes** |
| 0092 | Bind the MHI EFS channel as `WWAN_PORT_EFS` | **Yes** |

Camera fixes to keep: 0077, 0080, 0082, 0083, 0085, 0086, 0088 (+ reverts 0081,
0089, 0090). Modem fixes to keep: 0012, 0022, 0023, 0024, 0037, 0091, 0092.

**Note on 0078.** It set 400 kHz on `cci1_i2c0`. 0083 then moved the sensor block
to `cci1_i2c1` and carried the rate with it, so `cci1_i2c0` is back at the 1 MHz
`sm8250.dtsi` default. 0078 is a historical no-op. If the GC5035 on master 0 is
ever brought up, it will likely want 400 kHz too.

Older deferred cleanup: 0073/0074 and 0075/0076 are cancel-pairs still listed in
`source=`; net effect zero.

---

## 5. REAR CAMERA MAP

From `kona-oem-camera-kebab_t0.dtsi`, cross-checked against the extracted
`kebab-19805-camera.dts`.

**Bus numbering on the phone:**

| Controller | DT label | master 0 | master 1 |
|---|---|---|---|
| `cci@ac4f000` | `cam_cci0` | **i2c-21** | **i2c-22** |
| `cci@ac50000` | `cam_cci1` | **i2c-23** | **i2c-24** |

**Sensors:**

| Sensor | Node | Bus | CSIPHY | vio / vdig / vana | MCLK / RESET / VANA gpio |
|---|---|---|---|---|---|
| **IMX586** wide main 48MP | `cam-sensor@0` | cci0 **m0** → i2c-21 | 0 | L4P / pm8150a_s8 / L7P | 94 / 78 / — (+139 CUSTOM1, 117 VDIG) |
| **IMX481** ultrawide 16MP | `cam-sensor@1` | cci0 **m1** → i2c-22 | 1 | L4P / L1P / pm8150a_bob | 95 / 84 / 114 |
| **GC02M1B** mono 2MP | `cam-sensor@4` | cci0 **m1** → i2c-22 | 2 | L4P / L4P / L5P | 96 / 90 / — (+74 RESET_NEW) |
| **GC5035** macro 5MP | `cam-sensor@3` | cci1 **m0** → i2c-23 | 3 | L4P / L2P / L6P | 97 / 92 / — |
| IMX471 front *(done)* | `cam-sensor@2` | cci1 **m1** → i2c-24 | 4 | L4P / L1P / pm8150a_bob | 98 / 144 / 156 |

**Voltages** (`rgltr-min` / `rgltr-max`, order vio/vdig/vana):

- IMX586: `0 / 1350000 / 2900000` (+2000000, 2800000 extra entries)
- IMX481: `0 / 1100000 / 3300000`–`3800000`
- GC02M1B: `0 / 1800000 / 2800000`
- GC5035: `0 / 1200000 / 2800000`
- IMX471: `0 / 1100000 / 3000000`–`3800000`

**EEPROM addresses observed** (userspace scan taken while L4P was still
always-on — will NOT answer now that 0082 gave the rails to the driver):

| Bus | Addresses | Belongs to |
|---|---|---|
| i2c-21 (cci0 m0) | `0x54`, `0x5c` | IMX586 wide |
| i2c-22 (cci0 m1) | `0x50`, `0x58` | IMX481 / GC02M1B |
| i2c-23 (cci1 m0) | `0x50`, `0x58` | GC5035 macro |
| i2c-24 (cci1 m1) | `0x54`, `0x5c` | IMX471 front |

**Rear sensor slave addresses are still UNKNOWN.** Do not assume a family
default — that cost two days on the front camera, which turned out to be `0x10`,
not the `0x1a` everyone assumed.

**Vendor module descriptors** in `odm.img` at `/lib64/camera/`, each parseable
with the layout in §2.3:

```
com.qti.sensormodule.semco_imx586.bin      (949216 B)
com.qti.sensormodule.qtech_imx481.bin      (241624 B)
com.qti.sensormodule.shine_gc5035.bin       (98904 B)
com.qti.sensormodule.holitech_gc02m1b.bin   (74360 B)
com.qti.sensormodule.truly_imx471.bin      (124416 B)  <- decoded, front
```

Mount: `sudo mount -o loop,ro -t erofs $KEBAB_WORKDIR/f13/odm.img /tmp/ko`

**Mainline driver availability — the hard part.** `imx471.c` is the **only**
driver in tree for any sensor in this phone. There is no `imx586.c`, `imx481.c`,
`gc5035.c` or `gc02m1b.c`. Bringing up a rear camera means writing a sensor
driver including mode tables, not just wiring DT. The user reports an
out-of-tree driver exists for the rear — that changes the calculus considerably.

**Recipe for a rear camera**, applying what worked on the front:

1. Parse that sensor's `com.qti.sensormodule.*.bin` for the power sequence
   (§2.3 layout) and `laneAssign`.
2. Wire DT on the correct bus and master from the table above, rails per the
   vendor node, sensor-side `data-lanes` 1-based.
3. Add the camss port for its CSIPHY index with `clock-lanes = <7>`, **0-based**
   `data-lanes`, `bus-type = <MEDIA_BUS_TYPE_CSI2_DPHY>`.
4. Add a temporary powered in-driver scan to find the real slave address before
   trusting any datasheet default.
5. `vdda-phy`/`vdda-pll` on camss are already wired by 0086 and shared.

---

## 6. Useful references

- **`sm8250-xiaomi-elish-common.dtsi`** and `sm8250-xiaomi-pipa-common.dtsi` are
  the other SM8250 boards with camss enabled. elish independently confirms
  `vdda-phy = vreg_l5a_0p88`, `vdda-pll = vreg_l9a_1p2` and the
  `clock-lanes = <7>` / 0-based `data-lanes` form. **Check these first** for any
  camss question — searching only `*.dts` misses them, they are `.dtsi`.
- `Documentation/devicetree/bindings/media/qcom,sm8250-camss.yaml` —
  `clock-lanes` and `data-lanes` are both `required` per port.
- `imx471.c` is genuine upstream (Intel 2025 / Kate Hsuan, Red Hat 2026), written
  for an Intel IPU laptop. It manages only `vana` by design; the board
  integration is ours (0082).
- Downstream Qualcomm camera source, GPL and readable:
  `$KEBAB_WORKDIR/recovery/kebab-kernel/techpack/camera/drivers/cam_sensor_module/`.
  `cam_sensor_utils/cam_sensor_cmn_header.h:132` has the power-seq enum.
- **`$KEBAB_WORKDIR/ref/apollo-packages/`** — Xiaomi Mi 10T, same SM8250 + SDX55
  fusion. The reference implementation for every modem question, and right about
  every point it covered. **Not yet diffed for MCFG state — see §9.**
- `$KEBAB_WORKDIR/bin/qmdl-f3.py` — offline parser for raw `.qmdl` DIAG captures,
  extracts 0x79 EXT_MSG. The live parser desyncs on 8 KB 0x99 LOG_F packets.
- `$KEBAB_WORKDIR/mcfg/` — staged, unused: `hw_DSDS_mcfg_hw.mbn`, `hw_SS_mcfg_hw.mbn`
  (SDX55/**FUSION**/LA, our exact platform) and six French `sw_*.mbn` including
  `sw_orange_fr.mbn`.

---

## 7. Relay and workflow gotchas

From 09-18/09-21, still true:

- Relay scripts run as `base64 -d | bash`, i.e. **bash reads the script from
  stdin**. A nested `ssh` eats the rest of the script and silently truncates
  execution. Always `ssh -n` for the hop to the phone. Symptom: exit 0, output
  stops partway.
- **`ssh -n` and a heredoc are mutually exclusive.** `-n` redirects stdin from
  `/dev/null`, so `ssh -n host 'bash -s' <<'EOF'` runs nothing and returns 0.
  Use `B=$(base64 -w0 script); ssh -n host "echo $B | base64 -d | bash"`.
- **Never put a multi-minute wait inside a relay command.** The relay is serial;
  one blocked command stalls every later one. Writing a stub `result_NNNN.txt`
  makes the watcher skip an abandoned command on restart.
- **Big payloads go through `push_NNNN.txt`, not inlined in `cmd_NNNN.txt`.**
  Inlining ~11 KB blew the Windows argv limit and dropped the connection. Format
  is two lines: local path relative to the outputs folder, then the remote
  destination.
- **`NOFLASH=1 kebab-update.sh` silently syncs the OLD modules.** It copies
  `/lib/modules` out of `chroot_rootfs_oneplus-kebab`, refreshed only as a side
  effect of `pmbootstrap flasher flash_kernel`. For a module-only change, extract
  from the built apk instead. A ~50 s kernel "build" means ccache did its job —
  check the apk mtime and module sizes, not the log duration.
- **`pmbootstrap build` zaps the buildroots afterwards.** Copy hand-built
  binaries out of `chroot_buildroot_aarch64/tmp/` immediately.
- **`apk add` on the phone always fails its mkinitfs trigger** with
  `ERROR: No kernel found in /boot`. Harmless and pre-existing — the kernel lives
  inside `/boot/boot.img` for the fastboot workflow. The packages do install.
- `pmbootstrap search` does not exist. Use `apk search -x <pkg>` on the phone.
- `[ -w /sys/... ]` is evaluated as the *calling* user, so testing writability
  before a `sudo tee` silently skips every root-only sysfs write.
- The phone's `v4l2-ctl` is cut down: no `--set-fmt-video-mplane`, no `-v`. Use
  plain `--set-fmt-video`, which handles multiplanar fine. busybox `fuser` has no
  `-v`; use `lsof`. wireplumber holds `/dev/video0` — stop it before raw capture.
- Leaving `vendor.img`/`odm.img` on loop devices upsets pmbootstrap's zap phase
  (`umount ... qemu-aarch64-static`, exit 32). Unmount and `losetup -d`.

Added 2026-09-22:

- **`find -xdev` does not cross mount points.** I used it to look for the
  `modem_pr` tree and wrongly concluded it was missing everywhere; it was on the
  separate `/mnt/modemfw` mount. Nearly sent me after a nonexistent bug.
- **Read logs chronologically, not by keyword grep.** Two wrong conclusions in
  one session came from grep (a phantom tqftpserv write bug, and a bad retrigger
  count — MCFG's second pass reads only `.dig` files, so counting `mcfg_sw.mbn`
  lines misses it). Grep finds what you already suspect; chronology shows what
  happened.
- **Check the last known-good state before believing a regression.** If the
  claimed-broken thing was also broken when things worked, it isn't the cause.
- **Verify writes by reading back, not by the writer's exit status.** A restore
  logged 2294 bytes written; STAT on the modem read 908.
- **busybox `read` has no `-s`.** Use `stty -echo`. `/bin/sh` here is busybox and
  the login shell is `/bin/ash`. Also no `ls --time-style`, limited `awk`, and
  `grep` needs `-a` on DIAG logs or it prints "binary file matches" and nothing.
- `mmcli -m 0` is not stable across re-probes — resolve the index every time.
  And **do not infer "PIN disabled" from a missing `enabled locks:` line**; that
  line vanishes when the modem isn't fully initialised. Read `PIN1 state` off the
  card instead.
- `qmicli --help-<svc>` documents key names loosely; the parser is stricter:
  `aid` not `application-identifier`, `platform|software` not `hw|sw`.
- **Guard any automated PIN entry on a full retry count.** PIN1 has 3 attempts;
  a naive retry loop with a wrong stored PIN PUK-locks the SIM in three boots.
- **Mask identifiers before anything is logged or pasted.**
  `--uim-get-slot-status` prints ICCID, `mmcli -m N` prints IMEI, NV 550 and the
  `tcxomgr` items are IMEI and RF calibration. Use
  `sed -E 's/[0-9]{9,}/<masked>/g'`. The 09-22 morning doc has a bare IMEI in it
  — worth scrubbing.
- `kebab-diag-capture` holds `/dev/wwan0qcdm0` **exclusively**. Stop it before any
  DIAG/EFS work, and note `kebab-imei-restore` wants the same port. To capture the
  modem's boot window you must start the capture early (`After=systemd-udevd`,
  polling for the node) and get `kebab-imei-restore` out of the way; `systemctl
  mask` fails when the unit file lives in `/etc/systemd/system/`.

---

## 8. Known-noisy, deliberately not chased

- `qcom_q6v5_pas 5c00000.remoteproc: Handover signaled, but it already happened`
  — SLPI, quieted by 0037.
- `wcd938x_codec ... ASoC error (-16)` at boot.
- DSI PLL lock warning at 0.63 s; `dsi_err_worker: status=4` bursts.
- `mhi1` M0/M1 churn in dmesg is the **ath11k WiFi** (QCA6390, PCI 0x1101), not
  the modem. The modem is `mhi0` (0x0306 on bus 0002). Easy to misread.
- MM plugin load warnings for `libmm-plugin-mtk*` / `libmm-shared-mtk` /
  `libmm-plugin-rolling` — missing symbols, unrelated, harmless.
- `could not grab port wwan0qcdm0 / wwan0mbim0: unhandled port type` — MM's
  qcom-soc plugin doesn't handle wwan-subsystem QCDM/MBIM. Harmless.
- A **boot hang** was seen once on 2026-09-21 (stuck on kernel messages, forced
  power cycle). `journalctl -b -1` showed early userspace with plymouth and the
  slpi/cdsp/adsp remoteprocs coming up. Not attributable to anything we added —
  watch whether it recurs.
- Front camera image quality is untuned — no libcamera tuning file for IMX471 on
  this board. Frames work; colour/exposure is a separate exercise.

---

## 9. Where to pick up

1. **Modem/GPS — start with the apollo diff, not more instrumentation.**
   `$KEBAB_WORKDIR/ref/apollo-packages/` is the same SM8250+SDX55 fusion platform and is
   reported to have working mobile data. Nobody has compared its MCFG state
   against ours: its `/nv/item_files/mcfg/` contents, whether it ships or
   activates an `mcfg_sw` config, and its `mcfg_autoselect_by_uim` setting. That
   is the highest-value untried step.
   Then: decode what `MCENTRIC input 1` means (enum strings in
   `$KEBAB_WORKDIR/f13` `qdsp6sw.mbn` or a downstream `mcfg` header); and read
   `mcfg_setting` (41 B) / `mcfg_setting_1`, not yet looked at.
   **Success test:** `sudo qmicli -p -d qrtr://3 --dms-get-capabilities | grep
   Networks` — anything listing RATs means solved, and data plus GPS follow.
2. **Rear camera:** §5 has the full map and a recipe. Biggest cost is that no
   mainline driver exists for any rear sensor.
3. **Bisect the SIM-killer** to name whether `device_config.xml` or
   `generic_band_restrictions.xml` is the one that stalls MCFG. Two boots.
4. **Port the rest of the apollo stack** (MM `77-mm-sdx55-fusion.rules` is in
   place; still missing `pd-mapper`, the two MM patches,
   `zz-apollo-multiplex.conf`).
5. Reconcile the backported `imx471.c` against current upstream; it now carries
   local power-sequencing changes (0082).
6. Reconcile `boot.img` with the modules: the running `boot.img` is pkgrel 16
   while `/lib/modules` is pkgrel 18. Harmless (vermagic is `7.2.0` either way and
   only module internals changed) but worth flushing next time you are in
   fastboot.
7. Consider disabling `kebab-diag-capture` for daily use — it is diagnostic only
   and holds the DIAG port.
