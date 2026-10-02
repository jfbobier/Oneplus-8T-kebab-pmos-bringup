# Upstreaming plan

Prepared for review. Nothing here has been pushed or proposed to anyone.

## STOP — read this first

**postmarketOS (now renamed [Nura](https://nura.eco)) forbids AI-assisted
contributions.** From
<https://docs.nura.eco/policies-and-processes/development/ai-policy.html>:

> **We forbid the use of generative AI tools in Nura.**
>
> The following is not allowed in Nura:
>
> * Submitting contributions fully or in part created by generative AI
>   tools to Nura.
> * Recommending generative AI tools to other community members for
>   solving problems in the Nura space.

Repeated violation escalates under the Code of Conduct enforcement
guidelines, up to permanent bans. There is no disclosure exemption — it
is a prohibition, not a transparency requirement.

This tree was refactored with substantial AI assistance, which its own
git history records openly. **Destinations 1, 2 and 3 below are therefore
not actionable as prepared.** That covers all of
`gitlab.postmarketos.org`: pmaports *and* the `soc/qualcomm-sm8250/linux`
kernel fork.

What remains possible:

* **Keep the work here.** This repository is public and complete; anyone
  running a kebab can use it directly. That costs nothing and breaks no
  rules.
* **Redo it yourself, if you want it in Nura.** Not "reword it" — the
  policy's own reasoning is about authors who do not fully understand
  their contribution, cannot describe it accurately, and have not tested
  it. Satisfying that means the work has to genuinely be yours. That is a
  real option, and the findings in `REGRESSIONS.md` are a map of what to
  re-derive, but it is work, not paperwork.
* **Destination 4, mainline Linux, is a different project** with its own
  rules. Check them before assuming — do not carry the assumption across
  from here.

The technical findings below stand on their own merits regardless of who
can submit them, which is why the rest of this document is left intact.

## The short version

**Most of this does not go to pmaports.** The sm8250 kernel aport in
pmaports carries *zero* patches — its whole `source=` is the tarball and
the config:

```
source="
	$pkgname-$_tag.tar.gz::https://gitlab.postmarketos.org/soc/qualcomm-sm8250/linux/-/archive/$_tag/linux-$_tag.tar.gz
	$_config
"
```

Kernel changes for these devices go to the **kernel fork**,
`soc/qualcomm-sm8250/linux`, and reach pmaports later as a `_tag` bump.
So there are four destinations, not one:

| # | Destination | What | Status |
|---|---|---|---|
| 1 | **pmaports** `device-oneplus-kebab` | the userspace — UCM, udev rules, systemd units, Bluetooth address | prepared, CI green — **blocked by the AI policy** |
| 2 | **kernel fork** `7.2.0-dev` | the 6 new drivers, the board devicetree | all 18 patches apply cleanly — **blocked by the AI policy** |
| 3 | **pmaports** `linux-postmarketos-qcom-sm8250` | config changes, `_tag` bump | prepared, CI green — **blocked by the AI policy** |
| 4 | **mainline Linux** | the 9 generic fixes | independent, highest value |

That order was the plan before the policy was found. See the stop
notice above.

---

## 1. pmaports: the device package — prepared

Branch `kebab/device-userspace` in a clean clone at `~/pmaports-mr` on the
build host, one commit off `upstream/main`:

```
c8812e7fb9 oneplus-kebab: add audio and modem userspace
```

Upstream has three files (`APKBUILD`, `deviceinfo`,
`sm8250-oneplus-kebab-empty.dts`) at `pkgver=6`. This adds eleven and
bumps to `pkgver=7`, `pkgrel=0`, in two subpackages so a plain install
pulls in neither:

* `-audio` — the ALSA UCM profile
* `-modem` — ModemManager rules, the UIM provisioning unit, the PCIe2
  runtime-PM pin
* base — the per-device Bluetooth address service and the documented MHI
  debug modprobe option

**`deviceinfo` is byte-identical to upstream**, so nothing in this MR
touches it, and `dint` has nothing to complain about.

Verified:

```
pmbootstrap -p ~/pmaports-mr ci dint apkbuild-lint verify-checksums ec shellcheck
  -> all 5 pass, exit 0 ("apkbuild-lint: Success")
pmbootstrap -p ~/pmaports-mr build device-oneplus-kebab --force
  -> Done!
```

### Before this is proposed

* **The maintainer is not you.** `maintainer="Frieder Hannenheim
  <git@fhannenheim.net>"`, and the approval rules require the package
  maintainer's approval, or any two approvals after two weeks of no
  reply. Worth a courtesy note to them before opening the MR, and worth
  deciding whether you want to be added as co-maintainer — this is a
  substantial amount of device-specific policy to own.
* **Commit authorship is a placeholder.** The commit is authored as
  `jfbobier <jfbobier@users.noreply.github.com>`. pmaports commits should
  carry your real name and the address you use on
  gitlab.postmarketos.org. Fix with
  `git commit --amend --author="Name <email>"`.
* **Wiki.** The `wiki` CI check verifies devices are documented. kebab is
  already in the wiki; if the MR changes what works, the wiki page should
  change with it.

---

## 2. Kernel fork: drivers and the devicetree

Repo `https://gitlab.postmarketos.org/soc/qualcomm-sm8250/linux`, default
branch **`7.2.0-dev`** (the fork keeps `X.Y.0` and `X.Y.0-dev` per
version; 20 forks exist, so MRs are the normal route).

**All 18 patches apply cleanly to `7.2.0-dev`** — checked with
`scripts/try-patches.sh` against the branch archive, `applies=18 fuzz=0
conflict=0`. No rebasing work.

They are already one-commit-per-change in the working tree at
`.work/linux-sm8250-7.2.0` (branch `dp-debugfs-oops`), which is the right
shape for an MR; they need rebasing onto the fork's history rather than
our tarball baseline.

### The devicetree is the biggest item

| | lines |
|---|---:|
| fork's current `sm8250-oneplus-kebab.dts` | 832 |
| ours | 1959 |

The fork currently describes no display or panel, no touchscreen, no
audio, no camera, and only the PCIe side of the external modem. Ours adds
all of that plus charging and the full pinctrl. This is the change that
actually makes the device work for everyone else, and it is the one to
get right.

**It should go to mainline too.** `sm8250-oneplus-kebab.dts` exists in
mainline `linux-arm-msm`; the fork is a staging ground, not the
destination. Splitting the devicetree by subsystem (display, then touch,
then audio, …) is how it will be reviewable — one 1100-line commit will
not be.

---

## 3. pmaports: the kernel aport

**Careful: three devices share this aport and its single config file** —
`oneplus-instantnoodlep`, `oneplus-kebab`, `xiaomi-elish`. A config change
here lands on all three.

Our config differs from upstream's in three distinct ways, and they must
be separated:

### 3a. Enables our new drivers — blocked on #2

These name Kconfig symbols that do not exist until the fork MR merges.
Proposing them earlier breaks the build:

```
CONFIG_DRM_PANEL_SAMSUNG_AMB655X=y      patch 0001
CONFIG_TOUCHSCREEN_SYNAPTICS_TCM_ONCELL=y  patch 0002
CONFIG_REGULATOR_MP2762_OTG=y           patch 0003
CONFIG_SND_SOC_TFA2=m                   patch 0006
CONFIG_VIDEO_IMX471=m                   patches 0008/0009
CONFIG_MHI_BUS_SAHARA_MODEM=m           patch 0010
```

### 3b. Plain upstream symbols — can go now, independently

Nothing device-specific and nothing that needs a patch. The first one is
worth its own MR:

```
CONFIG_TYPEC_DP_ALTMODE=y
```

Upstream has this **unset**, which means DisplayPort over USB-C cannot
work on *any* of the three devices, all of which have a USB-C port and the
QMP combo PHY. That looks like a plain oversight rather than a decision.

```
CONFIG_WWAN=y  CONFIG_MHI_WWAN_CTRL=m  CONFIG_MHI_WWAN_MBIM=m
CONFIG_MHI_NET=m  CONFIG_RMNET=m        # modem transport
CONFIG_DEVFREQ_THERMAL=y                # GPU/devfreq thermal throttling
CONFIG_WATCHDOG_SYSFS=y
```

### 3c. Bring-up debugging — drop these from any MR

These are ours for diagnosing this device and should not be imposed on
three devices' users. They will be challenged in review, correctly:

```
CONFIG_SOFTLOCKUP_DETECTOR=y
CONFIG_DETECT_HUNG_TASK=y
CONFIG_DEFAULT_HUNG_TASK_TIMEOUT=10
CONFIG_RCU_CPU_STALL_TIMEOUT=8          # upstream: 21
CONFIG_NETCONSOLE=m
```

`CONFIG_MHI_BUS_DEBUG=y` is debatable — it only adds the debugfs
interface and enables no prints (see NOISE-3), so it is cheap and useful
for modem work on all three devices. Propose it, but expect to justify it.

The aport's maintainer is `Jianhua Lu <lujianhua000@gmail.com>`, with
`bluebunny <bluebunny@postmarketos.org>` as co-maintainer.

---

## 4. Mainline Linux: the nine generic fixes

These have no kebab specifics and are the most durable thing to come out
of this work. Each is already written as an upstream-shaped commit with
the reasoning in the message.

| patch | subsystem | list |
|---|---|---|
| 0004 | `drm/msm/dp` wide-bus vs link bandwidth | dri-devel, freedreno |
| 0014 | `drm/msm/dp` link-training fallback vs the HPD block | dri-devel, freedreno |
| 0015 | `drm/msm/dp` NULL deref in the test debugfs files | dri-devel, freedreno |
| 0005 | `usb/typec/altmodes` defer instead of failing when not DFP | linux-usb |
| 0016 | `usb/typec/qcom` no-op `tx_sig` IRQ logged as an error | linux-usb |
| 0007 | `ASoC/wcd938x` TX channel HPF init | alsa-devel |
| 0017 | `ASoC/wcd938x` impedance ramp with no headset logged as an error | alsa-devel |
| 0012 | `bus/mhi` honour `no_m3` at mission mode | mhi, linux-arm-msm |
| 0018 | `remoteproc/qcom_q6v5` repeated handover logged as an error | linux-remoteproc |

0014 is the one worth leading with: DP link-training fallback is
unreachable on *every* Type-C DP board, not just this one, and the fix is
four lines.

Note on 0013 (`net/wwan` EFS port type): it adds a value to the
`wwan_port_type` enum in `include/linux/wwan.h`, which is a UAPI-adjacent
change needing netdev review. Carry it in the fork; do not lead with it.

---

## Our own new packages are not ready to propose

`packages/kebab-modem-tools` and `packages/tqftpserv-sdx55` both need work
before they could go anywhere:

* **No `maintainer=` variable.** They carry a `# Maintainer:` *comment*
  with `kebab@example.com`, a placeholder. aports need the variable form
  with a real, contactable address — a noreply address will not do for a
  package bug report.
* `url=` points at `github.com/royka1/Xiaomi-Apollo-pmOS-packages`, the
  project they derive from. Correct for provenance, but decide whether an
  aport should point at the upstream project or at yours.
* Neither has a home in pmaports yet. `install-to-pmaports.sh` puts them
  under `temp/`, which is the fork-from-Alpine staging area and not where
  they belong.

Since cellular data does not work yet, there is no hurry: these support a
modem path that is incomplete, and proposing them now would be proposing
infrastructure for a feature that does not function.

---

## Commit style

`COMMITSTYLE.md` in pmaports: packages under `device/` omit the directory
prefix, and device-specific changes may use `manufacturer-codename:`:

```
oneplus-kebab: add audio and modem userspace
```

One device per commit for new devices; the kernel and firmware packages
for a new device go in the same commit as the device package. We are not
adding a device — kebab is already in `device/testing/` — so this is an
ordinary change, not a "new device" commit.
