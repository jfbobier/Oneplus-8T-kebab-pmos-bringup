# kebab (OnePlus 8T, SM8250 + SDX55) postmarketOS — MODEM handoff

**Status as of 2026-09-21: MODEM BOOTS TO MISSION MODE AND STAYS UP.**
The ~706 s ERRFATAL is fixed (§4.0). SIM and PIN work.

**The factory NV has now been restored** — container format cracked, written over
DIAG, verified byte-for-byte, and persisting across reboots including ~250 KB of
RF calibration (§4.4.4). The IMEI field went from absent to present.

**But the radio still will not come online.** `Bands: 'none'` is unchanged, so
something outside the OEMNVBK container also gates band capability. The leading
suspect is the RF *card* config (`/nv/item_store/rfnv/rfc.bl`, 5 bytes — looks
empty), which declares which bands the hardware supports and is not in the
container. GPS remains blocked by the same thing (§4.6).

Supersedes the modem sections of `kebab-handoff-MASTER-2026-09-17.md` and
`kebab-handoff-MASTER-2026-09-18.md`, both of which are **wrong about the
modem**. The camera sections of the 09-18 doc are still current.

- **Next free relay command number: 0319.**
- **Next free patch number: 0093.**
- pkgrel now **18** (was 16). Kernel tree HEAD unchanged: `e5db13c6f`.
- New patches this session:
  - `0091-kebab-sdx55-fusion-no-m3.patch` — stop MHI M3 autosuspend (§3)
  - `0092-kebab-mhi-bind-efs-channel.patch` — bind the EFS channel (§4)
- **Both are module-only. No `boot.img` flash was needed for any of this.**

---

## 1. Correcting the record

Two claims in the older docs were stale and cost time to re-derive:

| Old claim | Reality |
|---|---|
| 09-17: "modem: incomplete, `mhi0` reaches SECONDARY BOOTLOADER" | Long superseded. It reaches **MISSION MODE** at ~14–17 s. |
| 0024's own comment: "stays in SBL: BHI_EXECENV reads 0x1 ... hypothesis under test" | **That hypothesis was correct and 0024 is the fix.** The ap2mdm GPIO hogs are live (`gpioinfo` shows lines 56/57 claimed) and the SBL jumps to AMSS. |
| 09-17 dmesg triage: "`pcieport ... retraining failed` / `mhi0 Linkdown` (SDX55 modem PCIe link)" | Not a bring-up failure. Those lines are the **aftermath** of the driver's failed recovery, logged long after a successful boot. |
| FACTS.txt "THE PLAN": add `qcom,sm8250-mpss-pas` + `remoteproc@4080000` | **Dead end — do not pursue.** SM8250 has no internal modem; `smp2p_modem_in/out` are absent from `sm8250.dtsi` for that reason. The SDX55 is external over PCIe and is driven entirely by MHI. |

**Method note.** The same trap as the camera: a stale negative result in a
handoff doc became the premise for the next session. Before trusting any
"doesn't work" claim here, re-read the live `dmesg` — it took one privileged
probe to overturn three of them.

---

## 2. What actually works now

The full flashless boot chain runs end to end:

```
PRIMARY BOOTLOADER
  -> BHI loads sdx55m/sbl1.mbn
SECONDARY BOOTLOADER            (~8.7 s)
  -> Sahara v2 on MHI ch 2/3, 15 images, every one END_OF_IMAGE OK
MISSION MODE                    (~14-17 s)
  -> channels DIAG, EFS, MBIM, QMI, IPCR, IP_SW0, IP_HW0
  -> /dev/wwan0qcdm0, /dev/wwan0mbim0, /dev/wwan0qmi0
  -> net mhi_swip0, mhi_hwip0
  -> ModemManager creates a modem on QRTR node 3
```

Verified live: `BHI_EXECENV: 0x2`, `EE: MISSION MODE`, link `8.0 GT/s PCIe x2`.

### 2.1 The Sahara image chain, in the order kebab's SBL asks for it

```
37 -> multi_image.mbn      (13232)     via mhi_cntrl->amss_image
34 -> multi_image.mbn      (13232)     via mhi_cntrl->amss_image
40 -> apdp.mbn             (13508)
36 -> multi_image_qti.mbn  (12616)
41 -> devcfg.mbn           (42515)
25 -> tz.mbn               (913408)
33 -> hyp.mbn              (84416)
42 -> sec.elf              (12368)
23 -> aop.mbn              (154672)
16 -> efs1.bin             (2097152)   per-device NV
17 -> efs2.bin             (2097152)   per-device NV
20 -> efs3.bin             (2097152)   per-device NV
29 -> acdb.mbn             (131112)
 8 -> qdsp6sw.mbn          (84099292)  the modem payload
 6 -> apps.mbn             (2632704)
```

`amss_fw = "sdx55m/multi_image.mbn"` in the fusion `dev_info`, so IDs 34 and 37
both resolve to the manifest. That is load-bearing: leaving 36/37 unmapped sent
them to the `qdsp6sw.mbn` default and handed the SBL an 84 MB image where a
~12 KB manifest was expected.

Firmware lives in **`/lib/firmware/sdx55m/`** (no `qcom/` prefix) — the Sahara
loader builds paths from the MHI device name.

### 2.2 The EFS images served at boot (NOTE: they are blank — see §4.4.1)

The phone had been fed the 512-byte dummy `efs{1,2,3}.bin` from `modem.img`.
This device's own 2 MiB NV was already backed up at `~/kebab/efs-stage/` (from
`mdm1m9kefs{1,2,3}`, i.e. `/dev/sdf7`, `/dev/sdf8`, `/dev/sdf6`) and is now
installed at `/lib/firmware/sdx55m/efs{1,2,3}.bin`, mode 600. Confirmed served
at 2097152 bytes each.

**These partitions turned out to be empty** (179 non-zero bytes in efs1, zero in
efs2/efs3) — see §4.4.1. The device's provisioned NV is in `mdm_oem_stanvbk`
instead. The files are still per-device and must never be committed or shipped.

### 2.3 ESOC GPIOs, from the stock f13 DT

`boot.img.extracted.dtb.dts:3003` — `mdm0: qcom,mdm0`, `compatible = "qcom,ext-sdx55m"`:

```
qcom,mdm2ap-errfatal-gpio = <&tlmm  1 0>
qcom,mdm2ap-status-gpio   = <&tlmm  3 0>
qcom,ap2mdm-status-gpio   = <&tlmm 56 0>   (0x38)
qcom,ap2mdm-errfatal-gpio = <&tlmm 57 0>   (0x39)
```

Patch 0024 hogs 56 high and 57 low. That matches the vendor map and is
sufficient to get the SBL to jump — full ESOC was never needed for boot.

---

## 3. Patch 0091 — the M3 bug (fixed)

### 3.1 Symptom

Modem reached mission mode, then runtime-suspended to M3 after 2 s idle. It
woke correctly seven or eight times (~99 s apart), then a resume timed out:

```
mhi mhi0: Did not enter M0 state, MHI state: M3, PM state: M3->M0
mhi-pci-generic 0002:01:00.0: failed to resume device: -5
mhi-pci-generic 0002:01:00.0: device recovery started
pcieport 0002:00:00.0: broken device, retraining non-functional downstream link at 2.5GT/s
pcieport 0002:00:00.0: retraining failed
mhi-pci-generic 0002:01:00.0: Recovery failed: -25
```

After that the endpoint is gone until a **cold boot** — a warm `reboot` does
recover it in practice, but the link never re-trains within the running kernel.

### 3.2 Fix

The tree's `mhi_pci_dev_info` already carries a `no_m3` flag; the fusion profile
simply never set it. Patch 0091:

1. `.no_m3 = true` on `mhi_qcom_sdx55_fusion_info` — probe then skips
   `pm_runtime_set_autosuspend_delay` / `use_autosuspend` / `put_noidle`.
2. Stores `no_m3` in `struct mhi_pci_device` and guards the
   `MHI_CB_EE_MISSION_MODE` case in `mhi_pci_status_cb()`.

**Step 2 is essential and non-obvious.** `mhi_pci_status_cb()` calls
`pm_runtime_allow()` unconditionally on mission mode, which re-arms autosuspend
a few seconds after probe deliberately declined it. It also silently defeats any
attempt to pin `power/control` from userspace — a udev `ATTR{power/control}="on"`
rule and a systemd oneshot both got quietly undone this way before the cause
was found. Do not try to fix this from userspace.

### 3.3 Proof

15-minute soak with the userspace pins deliberately **removed**, so the kernel
patch was the only variable:

```
up=18.41 .. up=678.68   ctrl=on  link=8.0 GT/s PCIe/2  EE: MISSION MODE  M0: 1 M2: 0 M3: 0
```

`M3: 0` for the entire run; zero `M3` mentions in `dmesg`. The M3 failure mode
is gone.

---

## 4. SOLVED: the ~706 s ERRFATAL was an unserviced remote-EFS channel

### 4.0 The answer

The SDX55 on this board is a **flashless RMTEFS firmware build**: it has no
storage of its own and expects the AP to host its filesystem over **MHI channel
10/11 ("EFS")**, using the Sahara *memory-debug* protocol. Android does this
with `/vendor/bin/ks`.

Nothing was bound to `mhi0_EFS`. So:

1. At ~640 s the modem performs its first EFS-Sync and exports its EFS region.
2. Nobody answers. The sync never completes.
3. A fixed ~65 s later the modem **asserts MDM2AP_ERRFATAL** (gpio 1) and powers
   itself off, taking the PCIe link with it. Host-side that surfaces as
   `Device died` at ~706 s.

Three pieces were needed, and all three are required:

| Piece | Why |
|---|---|
| **Patch 0092** — bind the EFS channel in `mhi_wwan_ctrl` via a new `WWAN_PORT_EFS` type | creates `/dev/wwan0efs0`. Module-only, no flash |
| **`77-mm-ignore-sdx55-efs.rules`** (apollo's) | without it ModemManager AT-probes the new port and the modem ERRFATALs **within 2 s** of mission mode. This is not optional — adding the port without the rule is far worse than not adding it |
| **`mhi_efs_sync`** (apollo's) | actually speaks Sahara memory-debug on the channel |

### 4.0.1 Proof

Soak past 744 s, no ERRFATAL (gpiomon silent), no `Device died`, link steady at
8.0 GT/s x2, `EE: MISSION MODE` throughout. And at the exact moment the modem
used to go quiet:

```
[  643.432]   wrote 2097152 bytes to /var/lib/mhi-efs/m9kefs1
              -> RESET
[  643.442] session complete
```

The exported region is named **`m9kefs1`** — matching the partition label
`mdm1m9kefs1`, at kebab's 2 MiB size, exactly as apollo's notes predicted.

### 4.0.2 How ModemManager was caught in the act

```
[   16.707] [wwan0efs0/probe] probe step: AT open port
[   16.720] [wwan0efs0/probe] probe step: AT
[   18.372] [wwan0efs0/probe] probe step: AT close port
[   18.385] mhi-pci-generic 0002:01:00.0: Device died
```

apollo's rule comment is exact: *"ModemManager probes it with AT strings, the
modem's `rmts_srv` receives them as garbage and it ERRFATALs."*

### 4.0.3 The instrumentation that cracked it

Two things, both cheap, after five soaks had eliminated everything host-side:

- **`gpiomon -c gpiochip3 1 3`** on the two modem→AP sideband lines. The stock
  DT wires `mdm2ap-errfatal` on gpio 1 and `mdm2ap-status` on gpio 3, and they
  are unclaimed inputs, so userspace can watch them with no kernel change:
  ```
  705.843057676  rising   gpiochip3 1     <- the modem says "I crashed"
  707.043966404  falling  gpiochip3 1
  707.043999372  falling  gpiochip3 3     <- and powers off
  ```
  That single line turned "orderly shutdown" into "genuine crash" and redirected
  the whole investigation. **Do this first next time.**
- **`diag_reader <dev> <rawfile>`** — the second argv is a raw `.qmdl` dump. The
  live parser desyncs on the 8 KB `0x99` LOG_F packets the modem starts sending
  at ~633 s, i.e. exactly the window of interest. `~/kebab/bin/qmdl-f3.py`
  parses `0x79` EXT_MSG out of the raw file offline; that is what surfaced
  `EFS: EFS-Sync caller task: fs` as the last thing before the silence.

### 4.1 What was ruled out along the way (all five soaks)

With M3 eliminated the modem **still dies**, but with a different signature:

```
[  709.737800] mhi-pci-generic 0002:01:00.0: Device died
[  709.738228] mhi mhi0: Processing disable transition with PM state: Linkdown or Error Fatal Detect
[  713.914247] pcieport 0002:00:00.0: retraining failed
[  713.914704] mhi-pci-generic 0002:01:00.0: Recovery failed: -25
```

No resume failure, no M3, no `firmware crashed` callback. `Device died` comes
from `health_check()` → `mhi_pci_is_alive()`, which only does
`pci_read_config_word(PCI_VENDOR_ID)` and got `0xffff` — **so the PCIe link had
already dropped while the modem was awake in M0/mission mode.**

### 4.1 Five deaths, and what has been ruled out

| # | Config | Death | Signature |
|---|---|---|---|
| 1 | M3 on, ASPM on | 764.9 s | resume failure ("Did not enter M0"), M3 at 703.5 s + ~61 s timeout |
| 2 | **0091** (M3 off), ASPM on | 709.7 s | `Device died` |
| 3 | 0091, **ASPM fully off** | 707.9 s | `Device died` |
| 4 | 0091, ASPM on, **tqftpserv instance 3** | 708.3 s | `Device died` |
| 5 | 0091, **PCIe2 controller pinned** `power/control=on` | 706.1 s | `Device died` |
| 6 | 0091, tqftpserv, **SIM inserted + PIN entered**, **pm_service_native** | 708.0 s | `Device died` |

Deaths 2–6 land in a **4-second window (706.1–709.7 s)** across five very
different host configurations. This is a deterministic timer, not a flaky link.

**Ruled out, each by its own soak:**

- **MHI M3 / runtime PM** — patch 0091, `M3: 0` for the whole run. Dies anyway.
- **ASPM** — all of `l0s_aspm`, `l1_aspm`, `l1_1_aspm`, `l1_2_aspm`,
  `l1_1_pcipm`, `l1_2_pcipm` forced to `0` on the endpoint. Dies anyway, 1.8 s
  *earlier*.
- **PCIe2 host-controller runtime PM** — `1c10000.pcie` was
  `power/control=auto`; pinned it to `on` along with the root port. Dies anyway.
- **Missing RFS** — tqftpserv on QRTR instance 3 works and the modem uses it
  heavily (3700 log lines of mcfg reads). Dies anyway.
- **ath11k/WiFi interaction** — `mhi1` wakes on an independent ~99 s cadence and
  **keeps doing so at 803 s, long after the modem is dead**. In death 5 the
  modem died 0.02 s *before* the 706 s mhi1 wake. Coincidence, not cause.
- **No SIM / no subscription** — SIM inserted and PIN entered (MM went from
  `failed / sim-missing` to `locked`, then a SIM object appeared). Dies anyway.
- **Missing Peripheral Manager** — `pm_service_native` built and running from
  early boot (`announced service=53 version=7 instance=0`). Dies anyway.

**The modem's own DIAG log contains no ERRFATAL, no assert, no error of any
kind.** It logs normally (mcfg loading, thermal VADC, GPS XTRA3 URL selection),
goes **silent at 634 s**, and then the host's next config-space read at ~707 s
returns `0xffff`. diag_reader's own last line is
`[708.285] read error: I/O error`.

So: the modem stops logging ~73 s before it drops the link, and never complains.
That reads like an orderly, deliberate shutdown, not a crash.

### 4.2 The ESOC dead end — recorded so nobody repeats it

Before the EFS answer was found, the elimination above left "the AP never
completes the ESOC BOOT_DONE handshake" as the last standing hypothesis. **It
was wrong, and porting ESOC would have been a large waste.** Three independent
reasons, all checkable in the downstream source on the builder:

1. `ESOC_BOOT_DONE` **does nothing in hardware**. In
   `esoc-mdm-4x.c:mdm_notify()` it is only:
   ```c
   case ESOC_BOOT_DONE:
           esoc_clink_evt_notify(ESOC_RUN_STATE, esoc);
           break;
   ```
   an AP-internal notification to other kernel clients. No GPIO, nothing sent
   to the modem.
2. The stock kebab `qcom,mdm0` node wires **only four GPIOs** (1, 3, 56, 57).
   There is no `ap2mdm-wakeup`, `mdm2ap-wakeup`, `ap2mdm-vddmin`,
   `mdm2ap-vddmin`, `pblrdy`, `soft-reset` or `pmic-pwr-en` on this board —
   those appear in the driver's generic `gpio_map[]` but not in this DT. So
   there is no AP→modem keepalive or wake sideband to be missing, and patch
   0024 already holds both AP outputs at the levels ESOC would set.
3. `esoc.h` depends on the downstream SSR stack —
   `soc/qcom/subsystem_restart.h`, `soc/qcom/subsystem_notif.h`,
   `linux/ipc_logging.h`, and `struct esoc_clink` embeds `subsys_desc` /
   `subsys_device`. Mainline replaced all of that with remoteproc, so it is not
   a mechanical API port.

`mdm_helper_native` is built and installed but **inert** on kebab: it drives the
handshake through `/dev/subsys_mdm` and the `esoc_mdm_dbg_eng` debugfs request
engine, and reports
`timed out waiting for ESOC debugfs files under /sys/kernel/debug/esoc_mdm_dbg_eng/esoc0`.
Harmless; leave it disabled.

### 4.2.1 Historical: what the old hypothesis was

Patch 0024 statically *hogs* `ap2mdm-status` high, which is enough to make the
SBL jump to AMSS. But the real ESOC protocol is a dialogue: the vendor's
`/vendor/bin/mdm_helper` drives the AP2MDM sideband and **signals BOOT_DONE**,
and apollo replaces it with `mdm_helper_native` for exactly that reason. A
static GPIO level is not the same as the handshake.

A modem that boots, runs normally for ~11.5 minutes waiting for the AP to
acknowledge it, then powers itself down without logging an error, fits this
better than anything else — and it is the only thing not yet tested.

**`mdm_helper_native` cannot run on kebab as it stands.** It was built and
installed, and it fails immediately with:

```
timed out waiting for ESOC debugfs files under /sys/kernel/debug/esoc_mdm_dbg_eng/esoc0
```

It drives the handshake through the **downstream ESOC framework**, not GPIOs
directly — `/dev/subsys_mdm`, the `esoc_mdm_dbg_eng` debugfs request engine, and
the `ESOC_GET_STATUS` / `ESOC_GET_ERR_FATAL` ioctls (`ESOC_CODE 0xCC`), sending
`ESOC_BOOT_DONE`. None of that exists on kebab:

```
/dev/subsys_mdm                      -> No such file or directory
/sys/kernel/debug/esoc_mdm_dbg_eng   -> No such file or directory
/dev/subsys_*                        -> none
/sys/bus/esoc                        -> No such file or directory
```

This is the concrete gap between kebab and apollo. The apollo kernel is
described as "external SDX55 modem over PCIe/MHI (**esoc**, sahara, split FBC)".
kebab has sahara (0022/0023) and a static GPIO hog (0024) but **no esoc at all**.

### 4.2.1 The ESOC port (patch 0092) — this one needs a flash

Source is already on the builder, GPL and readable:
`~/kebab/recovery/kebab-kernel/drivers/esoc/`

```
esoc_bus.c           9678    the esoc bus type
esoc_dev.c          13693    /dev/esoc-* + /dev/subsys_* char devices
esoc-mdm-4x.c       35196    the qcom,ext-sdx55m driver proper
esoc-mdm-drv.c      19139    the state machine
esoc-mdm-pon.c       8247    power-on / reset sequencing
esoc-mdm-dbg-eng.c   8081    the debugfs request engine mdm_helper_native talks to
esoc.h / esoc-mdm.h / mdm-dbg.h / Kconfig / Makefile
include/uapi/linux/esoc_ctrl.h
include/linux/esoc_client.h
```

Scope of work:

1. Port those to the 7.2 tree (downstream is ~5.4-era; expect churn in
   `platform_device`, `gpio`/`gpiod`, `class_create`, timer and
   `of_*` APIs).
2. Add the `qcom,mdm0` DT node. The stock one is at
   `f13/boot.img.extracted.dtb.dts:3003`, `compatible = "qcom,ext-sdx55m"`,
   with the GPIO map already recovered in §2.3.
3. **Drop 0024's GPIO hogs** — ESOC claims lines 56/57 itself and will fail with
   `-EBUSY` against the hogs. 0024's own comment says exactly this.
4. Enable the Kconfig symbol and rebuild.

Steps 2 and 3 are device-tree changes, so **this is the point at which a
`boot.img` flash becomes necessary** (fastboot + the interactive prompt in
`kebab-update.sh`). Everything up to and including patch 0091 was module-only.

### 4.4 The remaining blocker: NV is not provisioned (IMEI all zeros)

ModemManager now names it exactly:

```
[modem0] couldn't load ESN: Device doesn't report a valid ESN
[modem0] couldn't enable interface: 'Couldn't set operating mode:
         QMI protocol error (52): 'DeviceNotReady''
```

`DeviceNotReady` is what a Qualcomm modem returns when asked to go online
without valid provisioned NV. The radio never powers on, so the "barred" signal
icon is a consequence of this, **not** an RF or antenna problem.

#### 4.4.1 Correction: the efs{1,2,3} images we serve are BLANK

Earlier in this session the real 2 MiB `mdm1m9kefs{1,2,3}` partitions were
copied to `/lib/firmware/sdx55m/efs{1,2,3}.bin` and confirmed served at 2 MiB
over Sahara. **The size was right; the content is zeros.**

| Image | Size | Non-zero bytes |
|---|---|---|
| `efs1.bin` | 2 MiB | **179** |
| `efs2.bin` | 2 MiB | **0** |
| `efs3.bin` | 2 MiB | **0** |

The live partitions still hash-match the 2026-09-07 backup, so nothing corrupted
them — they were always empty. That is consistent with §4.0: until today the
EFS channel was never serviced, so the modem could never persist anything to
them.

#### 4.4.2 Where the provisioned NV actually lives

| Partition | Size | Non-zero | Notes |
|---|---|---|---|
| `mdm_oem_stanvbk` (sda8) | 10 MiB | **45%** | magic **`OEMNVBK`** at offset 0, **234 × `/nv/item_files`** — this is the provisioned NV store |
| `mdm_oem_dycnvbk` (sda7) | 10 MiB | 2% | same `OEMNVBK` format, 7 items |
| `spunvm` (sde53) | 32 MiB | 208 KB | FAT (`MSDOS5.0`) volume |
| `modemst1` (sdf2) | 2 MiB | 4688 | |
| `modemst2`, `fsg`, `fsc` | | **0** | empty |

So the IMEI/RF calibration are in an **`OEMNVBK` container**, not in the EFS
images. Under OxygenOS something restores that into the modem; nothing in the
pmOS stack does.

#### 4.4.3 Good news: the modem's EFS now works, it just is not persisted

With the channel serviced the modem builds a full EFS in RAM and exports it:

```
exported m9kefs1: size=2097152  non-zero=2088405   (vs 179 in the image we feed it)
differing bytes: 2088403
```

So persistence is now only a plumbing question. Two ways, in increasing risk:

1. **No partition writes at all:** copy the exported region back over
   `/lib/firmware/sdx55m/efs1.bin` so Sahara serves the modem's own saved EFS
   next boot instead of a blank one. Reversible — it is just a file.
2. **Android's way:** `mhi_efs_sync -o /dev/disk/by-partlabel -g mdm1 -W`, which
   writes region `m9kefs1` onto partition `mdm1m9kefs1`. **This writes NV
   flash.** Back up first and diff the exported image against the partition.

Neither creates an IMEI on its own — that has to come from `OEMNVBK`.

#### 4.4.4 The OEMNVBK restore path — mapped, not yet solved

**Confirmed by QMI interrogation** (with MM stopped, `qmicli -d qrtr://3`):

| Query | Result |
|---|---|
| `--dms-get-band-capabilities` | **`Bands: 'none'`, `LTE bands: 'none'`** |
| `--dms-get-ids` | `IMEI: 'unknown'`, `ESN: '0'` |
| `--dms-get-activation-state` | `not-activated` |
| `--dms-get-operating-mode` | `offline`, `HW restricted: 'no'` |
| `--dms-set-operating-mode=online` | QMI 52 `DeviceNotReady` |
| `--dms-set-operating-mode=low-power` | QMI 60 `InvalidTransition` |
| `--dms-set-fcc-authentication` | QMI 57 `WmsInvalidMessageId` (not applicable) |
| `--nas-get-serving-system` / `--signal-strength` | work fine (−128 dBm, radio off) |
| `--uim-get-card-status` | **`Card state: present`, `usim`, `Application state: 'ready'`** |

`Bands: 'none'` is the real gate: band capability comes from RF calibration NV,
so there is nothing to transmit on. **Use `--dms-get-band-capabilities` as the
fast success signal** for any NV attempt — it needs no SIM, no registration and
no 12-minute soak, and it leaks no identifiers.

Two useful side effects: the QMI/NAS layer is fully functional, and the SIM is
completely provisioned, so apollo's `apollo-uim-provision` is **not needed**.

**FCC authentication is not the issue** — ruled out, one command.

##### The vendor mechanism

`/vendor/lib64/liboemnvbk_img_helper.so` (90 KB) is the only file in
`vendor.img` referencing both `mdm_oem_stanvbk` and `OEMNVBK`. It is loaded by
`/vendor/bin/mdm_helper` (and `mdm_helper_proxy`, `libmdmdetect.so`,
`libmdmimgload.so`). Exported entry points:

```
oem_nvbk_img_helper_main / _v2      oemnvbk_prepare_oemvnbktmp / _v2
oemnvbk_sync_partition / _v2        oemnvbk_add_map_config / _v2
setup_oemnvbk_daemon / _v2          oemnvbk_daemon_kill / _v2
read_oemnvbk_partition_img          oplus_read_oemnvbk_partition_img
oem_get_reserve_data                oem_hwrfvertable_getdict
oem_mcfg_get_fileinfo               oem_mcfg_verify_hash
crc_16_calc / Gen_CRC16 / crc_30 / crc_32
verifyData_raw_by_public_key        sdx55_BKDRHash
```

Flow, from its log strings:

1. Read board/RF identity: `/proc/oplusVersion/{prjName,RFType,pcbVersion,engVersion,oplusBootmode}`,
   `/proc/oplus_rf/rf_cable`.
2. Select the matching blob by board id — `find_the_board : %d`,
   `ignore nv file with rf_id %d, cur %d`,
   `inode info board id = %u, start sector = %u, sector number = %u, data size = %u`,
   `no hw/rf map for projet %s`.
3. Validate with **CRC-16**: `Invalid CRC for CNV data, %x`. The RSA/SHA256 code
   (`ftm_public_key_8250`, `RSA_verify_raw`) is for `oem_mcfg_verify_hash` on
   *mcfg* files, a separate concern. **The NV container is checksummed, not
   signed** — so rebuilding it is feasible.
4. Stage a Sahara-transferable image to `mdm1oemnvbktmp`:
   `no proper sahara header: %s`, `open OEM_SAHARA_TRS_FILE error %d`,
   `nv partition data size %u != sahara partition data size %u`.
5. Spawn `/odm/bin/oemnvbkdaemon`.

**`/odm/bin/oemnvbkdaemon` is a dead end** — 16 KB, no QMI/QRTR/socket/diag
strings at all, only `Listen file %s` / `sync %s` over `mdm1oemnvbktmp` and
`mdm_oem_dycnvbk`. It is a host-side watcher that keeps the dynamic NV backup in
sync; it does not talk to the modem.

##### `mdm1oemnvbktmp` is already populated on this device

`/dev/disk/by-partlabel/mdm1oemnvbktmp` (sde63, 2 MiB) still holds what
OxygenOS staged: 750385 non-zero bytes, saved to
`~/kebab/mdm1oemnvbktmp.img`. Structure:

```
+0x00 = 20 (0x14)        descriptor
+0x04 = 3                entry count
+0x08 = 472 (0x1d8)
+0x0c = 0x80400200       address
+0x10 = 0x001ffe00       length (2096640)
+0x14 = 0x001ffe00
+0x18 = 0x80600000       address
+0x20 = 0x80600000
0x0205  "19805"          <- kebab's own project id (androidboot.prjname)
0x0208  "OEMNVBK"        inner container (second at 0x27e)
0x0317  first /nv/item_files path
```

Records repeat as `{u16 len, u32 tag, ...}` with `0x10ff0901` / `0x10ff0902`
recurring as an apparent record marker, followed optionally by a NUL-terminated
path and a binary payload. **44 distinct `/nv/item_files/...` paths**, and they
are exactly the factory-calibration set:

```
mcs/tcxomgr/xo_factory_cal_data      mcs/tcxomgr/factory_cal_version
mcs/tcxomgr/ft_table_wwan            mcs/tcxomgr/ft_table_gps
mcs/tcxomgr/field_cal_params         mcs/tcxomgr/xo_crystal_type
gsm/gl1/gsm_rx_diversity             modem/lte/ML1/hpue_ulca_enable
modem/lte/rrc/PC2_WHITELIST.xml      mcfg/mcfg_autoselect_by_uim
gps/cgps/me/gnss_config              ... (44 total)
```

Note there is **no IMEI path** in that list — IMEI is a numbered NV item (550),
so it lives in one of the path-less records.

##### Tested and rejected: serving it as a Sahara EFS image

Copying `mdm1oemnvbktmp.img` over `/lib/firmware/sdx55m/efs1.bin` (Sahara image
ID 16) was verified installed (hash match, 750385 non-zero, `OEMNVBK` and
`19805` present) and **did not work** — bands still `none`. The reason is
instructive: the modem reads only **40 bytes** of image 16 and immediately sends
END_OF_IMAGE, with zeros *and* with the OEM image. It is probing a header, and
the OEM wrapper is not an EFS image. Reverted; blanks kept at
`/root/efs-blank-backup/`.

##### SOLVED: the OEMNVBK record format

Parser: **`~/kebab/bin/oemnvbk-parse.py`**. Inventory of the board's own variant
saved at `~/kebab/oemnvbk-inventory-19805.txt`.

| File | Records | Path records | Distinct paths | Variant streams |
|---|---|---|---|---|
| `mdm1oemnvbktmp.img` (board-selected) | 106 | 45 / 51 | **44 / 44** | 4 |
| `mdm_oem_stanvbk.img` (all boards) | 615 | **234 / 234** | **49 / 49** | **19** |
| `mdm_oem_dycnvbk.img` (dynamic) | 20 | 1 / 7 | 1 / 7 | 2 |

The two containers that matter are fully parsed. **`dycnvbk` is only partially
parsed (1 of 7 paths)** — it is the dynamic NV, 2.5% full, and uses `f2` values
(`0x81`, `0x83`, `0xe4`, `0xe5`) and a `group=0` not seen elsewhere, so it
probably has one more record variant. Not chased, because the factory NV in
`stanvbk` / `oemnvbktmp` is what the restore needs.

```
u32 length      total record length, including these 8 bytes
u8  kind        0x01 = numbered NV item
                0x02 = EFS file item (/nv/item_files/...)
u8  f1          varies: 0x09, 0x0d, 0x19, 0x1d, 0x40
u8  f2          varies: 0xff, 0xcc
u8  group       varies: 0x10, 0x18, 0x50, 0x70

kind 0x01:  u16 item_id, u16 data_len, u8 data[data_len]
kind 0x02:  u16 flags,   u16 path_len, char path[path_len],
            u8 data[length - 12 - path_len]
```

**Only the first byte identifies the record kind.** Two earlier attempts treated
all four bytes as a fixed tag (`0x10ff0901` / `0x10ff0902`) and therefore parsed
only the subset of records that happened to share the same `f1`/`f2`/`group` —
7 records, then 13, instead of 106. If a parse of this container looks
suspiciously partial, that is the bug.

Container layout: `OEMNVBK\0` header, then **one record stream per board/RF
variant**, appearing as offset clusters separated by large gaps.
`liboemnvbk_img_helper.so` selects one using
`/proc/oplusVersion/{prjName,RFType,pcbVersion}` and `/proc/oplus_rf/rf_cable`
(`ignore nv file with rf_id %d, cur %d`).

**Important shortcut: variant selection is already done.** `mdm1oemnvbktmp`
holds the variant OxygenOS chose for *this* board (project 19805). There is no
need to reimplement the board-matching logic — just use that partition's
contents. A copy is at `~/kebab/mdm1oemnvbktmp.img`
(sha256 `7756c099e88a92cb…`).

##### Inventory of what has to be restored

From `mdm1oemnvbktmp.img`: **106 records — 61 numbered NV items (54 distinct)
and 45 file items (44 distinct paths)**, in 4 variant streams.

`NV item 550 (NV_UE_IMEI_I)` is **present**, 10 bytes, twice.

The calibration set that explains both `Bands: 'none'` and the dead GNSS:

| Item | Size |
|---|---|
| `mcs/tcxomgr/xo_factory_cal_data` | 25 B |
| `mcs/tcxomgr/xo_factory_cal_debug` | 80 B |
| `mcs/tcxomgr/xo_crystal_type` | 8 B |
| `mcs/tcxomgr/ft_table_gps` | 772 B |
| `mcs/tcxomgr/ft_table_wwan` | 772 B |
| `mcs/tcxomgr/field_cal_params` | 74 B |
| `mcs/tcxomgr/field_aging_data` | 62 B |
| `mcs/tcxomgr/rgs_data` / `ifc_c1_filter` / `dcc_data` | 104 / 136 / 20 B |
| `modem/mmode/nr_band_pref` (+`_Subscription01`, `nr_nsa_*`) | 68 B each |
| `modem/lte/rrc/PC2_WHITELIST.xml` | 214 B |
| `modem/nr5g/RRC/cap_add_bw` / `cap_pc2_exception_control` | 777 / 764 B |
| numbered items 29451, 29570, 30006, 30016 (bulk cal) | 4804 / 56580 / 27918 / 42249 B |

Run `python3 ~/kebab/bin/oemnvbk-parse.py <file>` for the full list. The parser
deliberately prints **only** ids, paths and lengths — never payloads, since they
are the IMEI and per-device calibration.

##### The write side — mechanism WORKS, calibration still incomplete

Tool: **`~/kebab/bin/nvrestore.py`** (dry run by default; `--commit` to write,
`--only` to do one item, `--files-only` / `--items-only`).
Transport: DIAG on `/dev/wwan0qcdm0`.

**DIAG protocol details established empirically** (these cost time, write them down):

```
NV_READ_F  0x26   {u8 cmd, u16 item, u8 data[128], u16 status}
NV_WRITE_F 0x27   same layout. Payload is capped at 128 bytes.
                  status: 0=DONE 5=NOTACTIVE 6=BADPARM 7=READONLY

EFS2 = DIAG 0x4b, subsys 0x13, u16 subsys_cmd:
   2 OPEN     req {u32 oflag, u32 mode, char path[]}   rsp {i32 fd, u32 errno}
   3 CLOSE    req {u32 fd}                             rsp {u32 errno}
   4 READ     req {u32 fd, u32 nbytes, u32 offset}
              rsp {u32 fd, u32 offset, u32 nbytes, u32 errno, u8 data[]}
              NOTE: offset and nbytes are SWAPPED between req and rsp.
              Data starts at response offset 20. Getting this wrong makes
              every write look like MISMATCH when it actually succeeded.
   5 WRITE    req {u32 fd, u32 offset, u8 data[]}
              rsp {u32 fd, u32 offset, u32 nbytes, u32 errno}
   9 MKDIR    req {u32 mode, char path[]}  -- did NOT work as coded; the 11
              failures below persisted. Command code or payload still wrong.
  11 OPENDIR / 12 READDIR / 13 CLOSEDIR   -- OPENDIR works, READDIR parse unproven
  15 STAT     req {char path[]}            rsp {u32 errno, ..., u32 size@+12}
```

`EFS2_DIAG_HELLO` (cmd 0) timed out with every payload tried — **not needed**,
STAT/OPEN/READ/WRITE all work without it.

###### Results

| | Written | Verified byte-identical |
|---|---|---|
| EFS file items | 33 of 44 | **33** |
| Numbered NV items | 20 of 54 | **20** |

All the tcxomgr calibration files landed and verified: `ft_table_gps` (768 B),
`ft_table_wwan` (768 B), `xo_factory_cal_data` (21 B), `xo_crystal_type`,
`field_cal_params`, `field_aging_data`, `rgs_data`, `ifc_c1_filter`,
`ifc_version`, `dcc_data`, `tcxo_mode`, plus `nr_band_pref` ×4,
`cap_add_bw`, `PC2_WHITELIST.xml`, `cap_pc2_exception_control` ×2.

###### Persistence: solved for files, NOT for numbered items

The modem's EFS is RAM-backed from the Sahara `efs{1,2,3}.bin` images, so
runtime writes are lost on reboot. Fixed for files without any partition write:

1. write the NV via DIAG
2. let `mhi_efs_sync` export the region (it does so periodically, ~600 s)
3. `install -m600 /var/lib/mhi-efs/m9kefs1 /lib/firmware/sdx55m/efs1.bin`
   (and `m9kefs2` -> `efs2.bin`)
4. reboot

**Proof it works:** before, the modem read only 40 bytes of image 16 and skipped
it. After, it reads the whole thing:

```
READ_DATA image=16 offset=0       length=40
READ_DATA image=16 offset=512     length=1048576
READ_DATA image=16 offset=1049088 length=1048064
```

and after the reboot `ft_table_gps` (768), `xo_crystal_type` (4),
`xo_factory_cal_data` (21) and `nr_band_pref` (64) all STAT errno=0 at the right
sizes. **File NV now persists.**

**Numbered NV items do not.** After the same reboot, item 447 and 6853 are back
to `status=5` (NOTACTIVE). Legacy numbered NV evidently lives somewhere other
than the `m9kefs1`/`m9kefs2` regions we capture — possibly a third region the
modem never exported, or a separate store. Unresolved.

###### SOLVED: the RFNV route for the large items

The 17 large items are **not** legacy NV. The modem firmware names them itself —
`qdsp6sw.mbn` contains the literal string:

```
/nv/item_files/rfnv/
/nv/item_files/rfnv/00028874        <- 8-digit zero-padded decimal
```

So the convention is **`/nv/item_files/rfnv/%08d`**, written through the EFS2
file interface, which has no size limit. Two corroborating signals:

- every item that returned `NV_WRITE_F` status **6 (BADPARM)** is in the
  21xxx–30xxx range, i.e. it is RFNV, not legacy NV
- `/nv/item_files/rfnv` already existed on the modem (STAT errno=0)

**A trap worth recording:** STAT on `/nv/item_files/rfnv/00029570` returns
`errno=2`. That is *not* evidence the path is wrong — it is ENOENT because the
calibration was never restored, which is the whole problem. This was briefly
misread as "the naming convention is unknown".

`nvrestore.py` now routes any item over 128 bytes, or any item whose
`NV_WRITE_F` returned BADPARM, to the RFNV file path. Result: **all 28 RFNV
items written and byte-verified**, ~250 KB total:

```
00029570  56580 B   00030016  42249 B   00029619  37661 B   00030006  27918 B
00029623  23389 B   00029653  19180 B   00029620  16794 B   00030011  16699 B
00030007  11520 B   00030008   8744 B   00029622   8007 B   00030010   5109 B
00029451   4804 B   00029624   3793 B   00029654   2165 B   00029621   1594 B
```

STAT confirms them independently: `(0, 56580)`, `(0, 42249)`, `(0, 37661)`.
Total tally: **48 ok, 6 failed** (the failures being read-only item 1 and a few
legacy ids).

###### Persistence now covers everything, and survives reboots

`STATFS /nv` reports 4094 blocks of 512 B with 1772 free, so there is room. The
export-and-serve loop works for the whole set. After a reboot, all of this is
still present:

```
rfnv/00029570  (0, 56580)     ft_table_gps     (0, 768)
rfnv/00030016  (0, 42249)     xo_factory_cal   (0, 21)
rfnv/00029619  (0, 37661)
```

**Important timing detail:** the modem exports each region on its own schedule
(irregular, seen at 378 s, 643 s, 818 s, 1881 s, 3520 s of uptime). EFS2 SYNC
(cmds 26/41) got no response, so there is no known way to force it — you must
**wait for an export that is newer than your writes** before capturing and
rebooting, or the work is lost. Check
`journalctl -u mhi-efs-sync | grep 'wrote'` against the write time.

###### Progress on the IMEI

`--dms-get-ids` went from `IMEI: 'unknown'` to **`IMEI: '0'` with
`IMEI SV: '7'`** appearing. So NV item 550 partially took effect — the modem now
has an IMEI field where it previously had none. The value is still zero, almost
certainly because the container record is 10 bytes where `NV_UE_IMEI_I` is 9
(1 length byte + 8 BCD digits); the right 9 bytes need identifying within that
record.

###### Still blocked: `Bands: 'none'` unchanged

Radio state after all of the above is exactly as before: `Bands: 'none'`,
`IMEI: 'unknown'`, `activation not-activated`, `online` -> QMI 52
`DeviceNotReady`. The modem stayed healthy throughout (`Device died=0`).

Everything in the container is now on the modem and persisting, and the radio
still will not come online. So **something outside the OEMNVBK container also
gates band capability.**

The most promising lead is the RF *card* configuration, which the firmware names
in a **different tree** (`item_store`, not `item_files`) and which is **not in
the container at all**:

```
/nv/item_store                          (0, 3)
/nv/item_store/rfnv                     (0, 4)
/nv/item_store/rfnv/rfc.bl              (0, 5)     <- only 5 bytes
/nv/item_store/rfnv/rfnv.bl             (0, 20)    <- only 20 bytes
/nv/item_store/rfnv/iip2_path0_nb0.bin  (2, None)  <- absent
```

`rfc.bl` / `rfnv.bl` exist but at 5 and 20 bytes they look like empty indices
rather than a real RF card definition. On Qualcomm the RFC declares *which bands
the hardware physically supports* — separate from the per-unit calibration we
restored. If it is empty, `Bands: 'none'` is the expected result no matter how
good the calibration is.

Also absent and worth checking: `/policyman/device_config.xml`,
`/nv/item_files/modem/nas/lte_bandpref`, `/nv/item_files/modem/mmode/band_pref`.

##### DIAG capture after the NV restore — the RF stack is alive, POLICY is not

Step 3 below was done. The result reframes the remaining problem, and it is
**not** the RF card config.

**The RF stack now initialises.** SSIDs that were completely silent before are
now talking: 5000, 5004, 5006, 5030, 5032, 5034, 100, 110, 18, 14. And:

```
MPSS:Task rflm_lna_update # 5f0c465 begins grp 7      <- RF Link Manager running
```

**The calibration is being read successfully.** Every `RF EFS Read Error: File
not found!` in the capture is a *debug-only* path:

```
/nv/item_files/modem/rf/etdacrate_debug_enable   /nv/item_files/modem/rf/temp_comp
/nv/item_files/modem/rf/fbrx_dbg_mask_crash      /nv/item_files/modem/rf/txfed_dbg_mask_*
/rf/debug/sat_det/sat_det_enable                 /rf/hivswr_efsctrl.bin
```

There are **no calibration read errors**. So §4.4.4's restore worked at the
level that matters.

**What is actually empty is the radio capability, reported by Policy Manager:**

```
Prio 0: {Sub 0, State Baseline, Num_Techs 0, Tech0 0, Tech0_state Invalid State }
subs 0: CA band combo ''
```

`Num_Techs 0` means policyman believes the device supports **no radio access
technologies at all**. That is upstream of bands, and it explains
`Bands: 'none'` and `DeviceNotReady` far better than a calibration gap does.

Policy Manager's inputs, from the firmware strings:

- its XML configurations are **compiled into the MPSS image**
  (`//components/rel/mmcp.mpss/10.0/policyman/configurations/...`), so they do
  not need restoring
- its data files are **MDB databases** on EFS:
  ```
  /mdb/policyman/mcc2bands.mdb        <- MCC -> supported bands
  /mdb/policyman/mcc2arfcn.mdb        /mdb/policyman/mcc2country.mdb
  /mdb/policyman/mcc2border.mdb       /mdb/policyman/plmn2cacombos_lte.mdb
  /mdb/policyman/location_polygons.mdb  /mdb/policyman/polygon2mccs.mdb
  ```
- `/policyman/persisted_items/` is an EFS dir it writes
- `/policyman/device_config.xml` — **STAT errno=2, absent**
- the firmware contains the failure string `device_config_overall_feature fail`

**So the next thing to chase is policyman, not RF.** Concretely:

1. STAT `/mdb/policyman/*.mdb` on the modem. If they are missing, find them in
   the f13 dump (`modem.img`, likely under `image/`) and write them via EFS2 —
   the same tool and route already proven.
2. Find out what provides `/policyman/device_config.xml` and whether this
   device needs one. `Num_Techs 0` with `State Baseline` suggests policyman fell
   back to a baseline policy with nothing enabled.
3. Also still open: the legacy numbered NV items do not persist, producing a
   long tail of `Bad NV read status 5 ... Initializing to default value` for
   DS/CDMA items. Benign individually, but it means the legacy NV store is not
   being captured by the export-and-serve loop.

The RF card config (`rfc.bl`, 5 bytes) is **de-prioritised** — with no
calibration errors in the log it is probably fine as-is.

##### Earlier next-step list (steps 1 and 2 still open)

1. Find where the RFC config comes from. It is not in `mdm_oem_stanvbk`, so it
   is either compiled into the MPSS image, delivered in another partition, or
   built by the RF driver at init from something we are not providing. Grep
   `qdsp6sw.mbn` around the `rfc.bl` / `item_store` strings and look for an RFC
   blob in the f13 dump.
2. Fix NV item 550 (9 bytes, not 10) so the IMEI reads correctly.
3. ~~Turn the DIAG capture back on for one boot~~ — **done, see above.**

###### Artifacts

```
~/kebab/bin/oemnvbk-parse.py            container parser (structure only)
~/kebab/bin/nvrestore.py                the restore tool
~/kebab/bin/efsread-probe.py            how the READ response layout was found
~/kebab/oemnvbk-inventory-19805.txt     full inventory for this board
~/kebab/mdm1oemnvbktmp.img              the board's own NV container
on the phone:
  /root/efs-blank-backup/               the original blank efs{1,2,3}.bin
  /root/efs-postwrite/                  EFS exports containing the written NV
  /lib/firmware/sdx55m/efs{1,2}.bin     now the post-write EFS (revert from above)
```

**To revert everything:** copy `/root/efs-blank-backup/efs*.bin` back over
`/lib/firmware/sdx55m/` and reboot. The modem returns to the known-good
stable-but-unprovisioned state. No host partition was ever written.

##### What a real solution still needs

Step 1 (parse) is **done** — see above. What remains is step 2 only.

1. ~~Parse the OEMNVBK record format~~ — done, `~/kebab/bin/oemnvbk-parse.py`.
2. A way to **write** them into the modem. Options, roughly in order of promise:
   - **DIAG** — we have a working DIAG channel. Qualcomm's `NV_WRITE_F` (0x27)
     handles numbered items and the EFS2 subsystem commands (0x4B/0x13) handle
     `/nv/item_files/...` paths. This is the mechanism legitimate repair tooling
     uses.
   - Build a valid **EFS2 image** for Sahara image 16. The modem's own exported
     `m9kefs1` (2088405 non-zero bytes, in `/var/lib/mhi-efs/`) is a real EFS2
     sample to reverse the format from.
   - QMI has no generic NV-write in libqmi's public API.

This is a substantial reverse-engineering project in its own right — bigger than
everything else in this document combined — and it writes **RF calibration** to
the modem, where a wrong value is worse than no value. Treat it as its own
effort, take a fresh `mdm_oem_stanvbk` / `mdm1oemnvbktmp` backup first, and keep
`--dms-get-band-capabilities` as the pass/fail signal.

#### 4.4.5 Superseded notes

This is where the original "decompile the blob" instinct finally pays off, just
for a different blob than expected. Find whatever in `f13/vendor.img` reads
`mdm_oem_stanvbk` and pushes it to the modem (an Oppo/OnePlus NV
backup/restore service), and recover:

- the `OEMNVBK` container layout (header at offset 0 is
  `4f 45 4d 4e 56 42 4b 00` then version/count fields; entries reference
  `/nv/item_files/...` paths)
- how items are written to the modem — most likely QMI, in which case
  `qmicli` may be able to drive it without any new kernel work

Only after that is registration worth testing again.

### 4.5 Superseded: earlier notes on the IMEI

`equipment id: 00000000000000` even with this device's real 2 MiB
`efs{1,2,3}.bin` confirmed served over Sahara, and with `pm_service_native`
running. A network will not register a device presenting an invalid IMEI, which
is the likely explanation for mobile data showing as "barred" once the SIM was
unlocked — i.e. probably **not** a radio problem.

Where the real IMEI lives is not yet established. Candidates on the phone, none
of which are currently served to the modem:

| Partition | Device | Size |
|---|---|---|
| `mdm1m9kefs1/2/3` | sdf7 / sdf8 / sdf6 | 2 MiB each — **these are what we serve as efs1/2/3** |
| `modemst1` / `modemst2` | sdf2 / sdf3 | 2 MiB each |
| `fsg` | sdf4 | 2 MiB |
| `fsc` | sdf5 | 128 KiB |
| `spunvm` | sde53 | **32 MiB** |
| `mdm_oem_stanvbk` | sda8 | 10 MiB — "modem OEM **stat**ic **nv** **b**ac**k**up" |
| `mdm_oem_dycnvbk` | sda7 | 10 MiB — dynamic NV backup |

`spunvm` and `mdm_oem_stanvbk` are the most promising by name and size. Also
note `/var/lib/tqftpserv` (the `/readwrite/` target) is **empty** — the modem
wrote `server_check.txt` and `mcfg.tmp` and then unlinked them, so it is not yet
persisting NV there. A third possibility worth ruling out: with
`androidboot.verifiedbootstate=orange` (unlocked bootloader) the modem may
refuse to expose the provisioned IMEI at all, which would make this unfixable
rather than unported.

### 4.3 Separately worth fixing either way

### 4.2 Separately worth fixing either way

`mhi_pci_recovery_work()` calls `pci_try_reset_function()`, which on this board
**permanently bricks the endpoint**. A modem stuck in mission mode is far better
than a dead link. Consider a flag to skip the reset path for the fusion profile,
so a transient fault stays recoverable.

---

## 4.6 GNSS / GPS — investigated, almost certainly blocked by the same NV

The GNSS receiver is part of the SDX55, so it came along with the modem work.
**The good news:** the location engine is *not* gated the way the cellular radio
is. `qmicli -d qrtr://3 --loc-start` returns
`Successfully started location tracking (session id 0)`, and MM reports:

```
capabilities: 3gpp-lac-ci, gps-raw, gps-nmea, cdma-bs, agps-msa, agps-msb
supported assistance: xtra
assistance servers: https://path{1,2,3}.xtracloud.net/xtra3Mgrbej.bin
```

### 4.6.1 What was done

- Enabled both sources: `mmcli -m $M --location-enable-gps-raw --location-enable-gps-nmea`,
  refresh rate 1 s.
- Fetched the XTRA assistance file the modem itself names (62503 bytes, the
  phone has working WiFi) and injected it:
  `mmcli -m $M --location-inject-assistance-data=/tmp/xtra3.bin` ->
  `successfully injected assistance data`.
- Watched for ~20 minutes.

### 4.6.2 Result: the engine runs, but acquires nothing

```
[N] locClnt_SetFixCriteria: ValidMask=0x1ff
[N] locClnt_StartFix for client = N, return value = 0     <- sessions start fine
PE2ME:Res#  G 0 Glo 0 Gal 0 Navic 0, ... FixAge 13         <- zero resolved
PE2ME: AlmNeed 0x0000000000400000                          <- still wants almanac
Usable      GPS 0x0 GLO 0x0 GAL 0x0, NonEx 0x0
GPS Directions Reset  SV 37, elev -128, elevunc 127        <- no SV directions
```

`mmcli --location-get` returns nothing at all, and **there are no CN0 / SNR /
measurement lines anywhere in the DIAG stream.**

### 4.6.3 CONFIRMED with sky view: it is the NV, not the sky

The window test was run. With the phone at a window, over ~25 minutes:

```
PE2ME:EphNeed 0x0000000000000000, POSG 0 Qos 1000, FixVal 0, FixFail 1 ...
PE2ME:Res#    G 0 Glo 0 Gal 0 Navic 0, MeAirborne 0, MotMode 0, FixAge 13
Usable        GPS 0x0 GLO 0x0 GAL 0x0, NonEx 0x0
loc-get-operation-mode -> standalone
```

Three things make this conclusive:

1. **`EphNeed = 0`** — the XTRA injection worked. The engine *has* its ephemeris.
   So this is not an assistance-data problem.
2. **Exactly one distinct value of `Res#` and of `Usable` appears in the whole
   log.** The measurement engine never emitted a single differing report. A
   receiver with a poor sky view produces varying CN0 values as it tries; this
   one is completely static.
3. **No CN0 / SNR / Doppler / PRN lines anywhere**, with sky view, in
   `standalone` mode, with fix sessions starting successfully
   (`locClnt_StartFix ... return value = 0`) and failing (`FixFail 1`).

Everything the engine needs is present and it produces no measurements at all.
That is a GNSS RF front-end that is not operating, which is what missing
XO/TCXO calibration produces: without the reference-frequency offset the search
windows are wrong and nothing is ever acquired.

### 4.6.3.1 GPS and cellular are the same bug

`Bands: 'none'` on the cellular side (§4.4) and zero GNSS measurements here both
trace to the same absent factory calibration in `mdm_oem_stanvbk`. **Solving the
NV restore (§4.4.4) fixes both; nothing short of it will fix either.** Do not
treat GPS as separate work.

### 4.6.3.2 Historical: why sky view was suspected

That last point is the discriminator. A receiver with a poor view of the sky
still reports CN0 values for candidate satellites — it sees them weakly and
fails to decode. Ours reports **no measurements whatsoever**, which looks like an
RF front-end that is not operating rather than one that is listening and
failing.

And the NV blob we cannot yet restore contains exactly the GNSS-critical
calibration:

```
/nv/item_files/mcs/tcxomgr/ft_table_gps
/nv/item_files/mcs/tcxomgr/xo_factory_cal_data
/nv/item_files/mcs/tcxomgr/xo_crystal_type
/nv/item_files/mcs/tcxomgr/factory_cal_version
/nv/item_files/gps/cgps/me/gnss_config
/nv/item_files/gps/cgps/me/gnss_multiband_config
```

Without the XO/TCXO frequency-reference calibration a GNSS receiver does not
know its own reference offset, so its search windows are wrong and it acquires
nothing regardless of sky view. **So GPS is most likely downstream of the same
§4.4 blocker, not independently fixable.**

(Done — see §4.6.3. It was not the sky.)

### 4.6.4 The wrong location you see now is geoclue, not GPS

`/etc/geoclue/geoclue.conf` has `[ip] enable=true method=ichnaea` and
`[wifi] enable=true`. With no GNSS fix, geoclue answers from IP/WiFi
geolocation, which is what produces an error of a few kilometres. Once a real
fix exists geoclue should prefer it (it ranks by accuracy); if it does not,
disable the `[ip]` source. Do **not** disable it before GNSS works or there will
be no location at all.

### 4.6.5 Housekeeping

`kebab-diag-capture.service` holds the DIAG port open and the log grows (5.3 MB
in ~25 min). Disable it when not actively debugging:
`sudo systemctl disable --now kebab-diag-capture`.

---

## 5. What the AP still owes the modem

`~/kebab/ref/apollo-packages/` (Xiaomi Mi 10T, same SM8250 + SDX55) is the
answer key. kebab currently has **none** of the following:

| Piece | Why | Status on kebab |
|---|---|---|
| `tqftpserv-sdx55` | RFS over QRTR. Two real upstream bugs: must `qrtr_publish(fd, 4096, **3**, 0)` (the PCIe SDX55 looks up instance 3, upstream publishes 1), and `tftp_send_data()` must retry on EAGAIN/ENOBUFS — at the negotiated blksize 7680 x wsize 10 block 8 fails every time and the modem ERRFATALs ~64 s later | **DONE 2026-09-21.** Built from apollo's sources, running as `tqftpserv-sdx55.service`, stock `tqftpserv` masked. Confirmed working: the modem writes `/readwrite/server_check.txt` and reads its whole mcfg tree, all `from 3:xx` (QRTR node 3) |
| `pm_service_native` | Answers the Peripheral Manager QMI service; the modem needs it before it will touch secure NV | **DONE 2026-09-21.** Built, enabled as `pm-service-native.service`, announces service=53 version=7 instance=0. Did not fix the §4 timer and did not produce a real IMEI |
| `mdm_helper_native` | Drives the AP2MDM sideband handshake, signals BOOT_DONE (replaces the vendor ESOC `mdm_helper`) | built and installed but **inert**: needs the downstream ESOC framework, which kebab does not have. See §4.2 / §4.2.1 |
| `mhi_efs_sync` | Replaces Android's `/vendor/bin/ks` on MHI channel 10 | absent |
| `rmtfs` | installed and enabled, but **skipped**: `ConditionPathExists=/dev/qcom_rmtfs_mem1`. kebab's DT has no `qcom,rmtfs-mem` reserved-memory node. Probably not needed for a PCIe modem (RFS goes via tqftpserv) — confirm before adding a node | installed, never starts |
| `pd-mapper` | apollo depends on it | **not in the pmOS repo** — needs an aport |
| MM udev rules | `77-mm-ignore-sdx55-efs.rules` (MM probes the EFS channel with AT strings; the modem's `rmts_srv` treats them as garbage and ERRFATALs) and `77-mm-sdx55-fusion.rules` (fuse the QRTR control plane and the mhi_net data plane into one device) | absent |
| MM patches | `0001-qcom-soc-mhi-net.patch`, `0002-bearer-bind-pcie.patch` | absent |
| `zz-apollo-multiplex.conf` | Without QMAP multiplexing the downlink wedges permanently after ~10 MB | absent |
| UIM provisioning | Only needed for a SIM in physical slot 2; the modem self-provisions in slot 1. MM reports **slot 2 active** on kebab | absent |

Installed this session: `qrtr`, `rmtfs`, `tqftpserv`, `libqmi`, `qmi-utils`.
`qrtr-ns` has no systemd unit in the `qrtr` aport.

**A SIM is now inserted and its PIN has been entered** (GNOME prompted for it).
MM went `failed / sim-missing` -> `locked` -> a SIM object appeared, and mobile
data could be enabled in the shell, but the signal shows as **barred**. See
§4.2.2 — almost certainly the all-zero IMEI rather than anything radio-side.

Current MM view: `plugin qcom-soc`, `primary port qrtr3`, ports
`mhi_hwip0 (net)`, `mhi_swip0 (net)`, `qrtr3 (qmi)`, `wwan0qmi0` ignored,
`revision Q_V1_P14`. MM's own complaints worth noting:
`could not grab port wwan0qcdm0/wwan0mbim0: unhandled port type`, and
`Couldn't check unlock status: QMI operation failed: GW primary session index unknown`.

---

## 6. Workflow gotchas found this session

- **`NOFLASH=1 kebab-update.sh` silently syncs the OLD modules.** The script
  copies `/lib/modules` out of `chroot_rootfs_oneplus-kebab`, which is only
  refreshed as a side effect of `pmbootstrap flasher flash_kernel`. With
  `NOFLASH=1` the build succeeds and stale modules get pushed. For a
  module-only change, extract from the built apk instead:
  ```sh
  tar xzf ~/.local/var/pmbootstrap/packages/edge/aarch64/linux-...-r17.apk -C /tmp/k17
  cd /tmp/k17/usr && tar czf /tmp/mods.tar.gz lib/modules/
  # scp, then on the phone: sudo tar xzf ... -C /usr/ && sudo depmod -a 7.2.0
  ```
  A ~50 s kernel "build" is the tell that ccache did its job — not that nothing
  was rebuilt. Check the apk mtime and the module size, not the log duration.
- **`~/kebab/patches/` is a stale partial archive** (stops at 0055). The
  canonical patch location is the aport:
  `~/.local/var/pmbootstrap/cache_git/pmaports/device/testing/linux-postmarketos-qcom-sm8250/`.
- **`pmbootstrap search` does not exist.** Use `apk search -x <pkg>` on the phone.
- **`ssh -n` and a heredoc are mutually exclusive.** The 09-18 doc says to use
  `ssh -n` for the hop to the phone (correct — a nested `ssh` otherwise eats the
  rest of the relay script from stdin), but `-n` also kills
  `ssh -n host 'bash -s' <<'EOF'`, which then runs nothing and returns 0. Use
  the relay's own trick instead: `B=$(base64 -w0 script); ssh -n host "echo $B |
  base64 -d | bash"`.
- **`apk add` on the phone always fails its mkinitfs trigger:**
  `ERROR: No kernel found in /boot (checked: vmlinuz*, linux.efi)`. Harmless and
  pre-existing — the kernel lives inside `/boot/boot.img` for the fastboot
  workflow, not as a bare `vmlinuz`. The packages do install; `boot-deploy`
  bails before writing anything.
- Phone busybox has no `ls --time-style` and a limited `awk`. Filter dmesg with
  `grep -n` + `sed -n 'a,bp'` rather than awk on timestamps.
- `grep` on the DIAG log needs **`-a`**: it contains binary and GNU grep
  otherwise reports "binary file matches" and prints nothing.
- **Never put a multi-minute wait loop inside a relay command.** The relay is
  serial — one blocked command stalls every later one, and the watcher has to be
  Ctrl+C'd to recover. (Writing a stub `result_NNNN.txt` by hand makes the
  watcher skip the abandoned command when it restarts.) Start a detached watcher
  on the phone, return immediately, and poll from the client side with short
  commands. This bit twice: a soak loop keyed on "uptime >= 980 s" never exited
  because the phone was rebooted for the SIM and its uptime reset.
- `[ -w /sys/... ]` is evaluated as the *calling* user. Testing writability
  before a `sudo tee` silently skips every root-only sysfs write — check by
  attempting the write under sudo and reading the value back instead.
- **Exposing a new wwan port means telling ModemManager to ignore it, in the
  same change.** Adding `wwan0efs0` without apollo's ignore rule made things
  dramatically *worse* (crash at 18 s instead of 706 s), because MM AT-probes
  every wwan port it sees. The rule was read early in the session and installed
  late; that cost two boots. apollo's `apollo-modem-support` rules are not
  optional extras, they are part of the mechanism.
- `pmbootstrap build` **zaps the buildroots afterwards**, which deletes anything
  you left in `chroot_buildroot_aarch64/tmp/`. Copy hand-built binaries out
  immediately, or rebuild them after every kernel build.
- When a change makes things worse, revert to the known-good state before
  theorising — but first run the one cheap single-variable test that tells you
  *which half* of the change did it (here: keep the patch, disable the daemon).

---

## 7. Where to pick up

The modem now boots and stays up. The one thing standing between that and
actual service is the IMEI.

SIM + PIN are confirmed working (`state: locked` -> unlocked). The blocker is
purely NV provisioning.

1. **Crack `OEMNVBK` (§4.4.4).** This is the critical path to a registering
   modem. Recover the container format and the restore mechanism from
   `f13/vendor.img`, then push the NV items into the modem (likely via QMI, so
   possibly no kernel work at all). Everything else below is secondary.
2. **Persist the modem's EFS (§4.4.3)**, starting with the no-risk option: copy
   the exported `m9kefs1` over `/lib/firmware/sdx55m/efs1.bin` so the modem gets
   its own saved EFS next boot rather than a blank image. Only move to
   `-g mdm1 -W` (which writes NV flash) once that is understood.
3. **Soak longer.** 1085 s with zero `Device died` clears the old failure point
   comfortably, but confirm it survives several EFS-Sync cycles (~600 s apart)
   and an overnight run.
4. Do the recovery-path change in §4.3 when convenient — module-only, and it
   stops a future fault from bricking the link until reboot.
5. Then the rest of the apollo stack for data: MM's `77-mm-sdx55-fusion.rules`,
   the two MM patches, `zz-apollo-multiplex.conf` (without QMAP multiplexing the
   downlink wedges permanently after ~10 MB), and `pd-mapper` (no aport yet).

### 7.0 Hygiene note

`mmcli -m 0` is not stable — ModemManager re-creates the modem object (and can
change its index) after a PIN unlock or a re-probe. Resolve the index first:

```sh
M=$(mmcli -L | sed -n 's#.*/Modem/\([0-9]\+\).*#\1#p' | head -1)
```

Also: `mmcli -i <n>` prints the SIM **ICCID**, and `mmcli -m <n>` prints the
IMEI. Filter or mask those in any captured output — they are subscriber and
device identifiers and there is no reason for them to end up in a log or a
handoff doc.

### 7.1 How the two working binaries were built

Both were cross-compiled straight into the aarch64 buildroot, as bare binaries
rather than aports, so no package install and no mkinitfs trigger:

```sh
CH=~/.local/var/pmbootstrap/chroot_buildroot_aarch64
sudo mkdir -p $CH/tmp/mbuild && sudo cp <sources> $CH/tmp/mbuild/
pmbootstrap -y chroot -b aarch64 -- apk add build-base qrtr-dev zstd-dev linux-headers
pmbootstrap -y chroot -b aarch64 -- sh -c 'cd /tmp/mbuild && \
  gcc -O2 -o diag_reader diag_reader.c && \
  gcc -O2 -DHAVE_ZSTD -o tqftpserv-sdx55 tqftpserv.c translate.c zstd-decompress.c -I. -lqrtr -lzstd'
```

Then scp to the phone: `diag_reader` -> `/usr/local/bin`, `tqftpserv-sdx55` ->
`/usr/bin`. Runtime deps (`libqrtr.so.1`, `libzstd.so.1`) are already present
from the `qrtr` and `zstd` packages.

`tqftpserv-sdx55.service` is apollo's, with one kebab change: this is an A/B
device, so the firmware partition is **`modem_a`** (FAT16, `/dev/sde4`), not
`modem`. It mounts at `/mnt/modemfw` and `image/modem_pr` is symlinked to
`/lib/firmware/qcom/sdx55m/modem_pr`, which is where tqftpserv's `translate.c`
maps `/readonly/firmware/image/...`.

### 7.2 Units currently installed on the phone

| Unit | State | Notes |
|---|---|---|
| `tqftpserv-sdx55.service` | enabled, active | keep |
| `tqftpserv.service` | masked | stock, publishes instance 1, useless here |
| `mhi-efs-sync.service` | enabled, active | **required** — services the remote-EFS channel. Safe mode (`-o /var/lib/mhi-efs`, no `-g`, no `-W`) |
| `/etc/udev/rules.d/77-mm-ignore-sdx55-efs.rules` | installed | **required** — without it MM's AT probe on `wwan0efs0` kills the modem in ~2 s |
| `kebab-diag-capture.service` | enabled, active | diagnostic only; `/var/log/diag_reader.log` + raw `/var/log/diag.qmdl`. Disable when not debugging — it holds the DIAG port open |
| `kebab-pcie2-no-runtime-pm.service` | enabled | pins `1c10000.pcie` + root port + endpoint to `power/control=on`. Did **not** fix anything — safe to drop |
| `kebab-sdx55-no-runtime-pm.service` | disabled | superseded by patch 0091, delete |

**A boot hang was observed once** on 2026-09-21 (stuck showing kernel messages,
needed a forced power cycle). `journalctl -b -1` showed it died during early
userspace with plymouth and the slpi/cdsp/adsp remoteprocs coming up;
`kebab-pcie2-no-runtime-pm` had **not** run, so it was not the cause. Treat as
an unexplained intermittent — worth watching whether it recurs.

Artifacts left on the phone: `/home/jeff/dmesg-boot-dummyNV.txt`,
`/home/jeff/mhi-watch-prePM-reboot.log`. Build log:
`~/kebab/build-0091.log`. Disabled userspace workaround kept at
`/root/80-kebab-sdx55-no-runtime-pm.rules.disabled` and
`/etc/systemd/system/kebab-sdx55-no-runtime-pm.service` (disabled) — both are
redundant now that 0091 is in, and should be deleted once 0091 is trusted.
