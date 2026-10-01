# Bumping to a new SM8250 kernel

The upstream fork tags `sm8250-<mainline major>.<minor>.0` and tracks
mainline majors, roughly every one to three months:

| tag | date |
|---|---|
| `sm8250-7.2.0` | 2026-08-29 |
| `sm8250-7.1.0` | 2026-07-05 |
| `sm8250-6.17.0` | 2025-11-11 |
| `sm8250-6.16.0` | 2025-08-02 |
| `sm8250-6.13.0` | 2025-01-20 |

So the next one is `sm8250-7.3.0`, and the only thing that changes in
`kernel/APKBUILD` is `pkgver` — `_tag="sm8250-$pkgver"` derives from it.
Note they skip releases (no 6.14, 6.15, or 7.0 tag), so do not assume the
next tag exists just because mainline released.

Nothing about the build or deploy process changes. What a bump risks is
only this: upstream may have **moved** code a patch touches, or **fixed**
a bug a patch fixes.

---

## 1. Point the aport at the new tag

```bash
cd ~/kebab-refactored/kernel
sed -i 's/^pkgver=7\.2\.0$/pkgver=7.3.0/' APKBUILD
sed -i 's/^pkgrel=.*/pkgrel=0/' APKBUILD
```

Reset `pkgrel` to 0: it counts rebuilds of one `pkgver`, and the device
drift check reads the full `pkgver-pkgrel`, so it does not care about the
numbering restarting.

## 2. Fetch the tarball and update its checksum

```bash
pmbootstrap checksum linux-postmarketos-qcom-sm8250
```

This downloads the source and rewrites the tarball hash in pmaports' copy
of the APKBUILD. Copy that one line back into `kernel/APKBUILD` —
everything else in `sha512sums` is a local file and unchanged.

**Regenerate the whole `sha512sums` block in `source=` order if you touch
it by hand.** abuild pairs the two lists *positionally*, not by filename;
see BUILD-3 in `REGRESSIONS.md` for the hour that cost.

## 3. Find out which patches survive — the only genuinely new step

```bash
tar xzf ~/.local/var/pmbootstrap/cache_distfiles/linux-postmarketos-qcom-sm8250-sm8250-7.3.0.tar.gz -C /tmp
scripts/try-patches.sh /tmp/linux-sm8250-7.3.0
```

Each of the 18 patches comes back as one of:

| verdict | what to do |
|---|---|
| `APPLIES` | nothing |
| `FUZZ` | applies with offsets — fine, but regenerate the patch so the next bump starts clean |
| `REDUNDANT` | the change is **already upstream**. Delete the patch, drop it from `source=` and `sha512sums=`, renumber the rest. This is a win. |
| `CONFLICT` | a human has to look |

**Expect `REDUNDANT` on the generic fixes**, and hope for it: 0004, 0005,
0007, 0012, 0014, 0015, 0016, 0017 and 0018 have no kebab specifics in
them and are the ones upstream can take. Each one that goes redundant is
one less patch to carry. Do not force a redundant patch back in.

The six add-only patches (0001, 0002, 0003, 0006, 0008, 0010) add a new
`.c` plus a `Kconfig`/`Makefile` hunk. They cannot conflict on content; if
one fails it is the Kconfig or Makefile context, which is a two-minute
fix. **0008 is special**: it is a verbatim upstream import of the IMX471
driver. If the new kernel ships `drivers/media/i2c/imx471.c` itself, drop
0008 entirely and rebase only 0009 (the OF and three-rail sequencing) on
top of the in-tree version. See CAMERA-2.

Watch 0013 specifically: it adds a value to the `wwan_port_type` enum in
`include/linux/wwan.h`, which conflicts if upstream added a port type.
The resolution is always "take both", never "take ours".

## 4. Migrate the kernel config

```bash
pmbootstrap kconfig migrate linux-postmarketos-qcom-sm8250   # runs make oldconfig
pmbootstrap kconfig check  linux-postmarketos-qcom-sm8250
```

`migrate` asks about every new symbol; `check` validates the result
against what pmaports requires (the `pmb:kconfigcheck-*` options in the
APKBUILD). Then copy the migrated config back to
`kernel/config-postmarketos-qcom-sm8250.aarch64`.

Two things to re-verify afterwards, because both have bitten this board:

* **`CONFIG_SND_SOC_TFA9872` must stay off.** `oldconfig` will offer it
  again. It matches the same `nxp,tfa9874` compatible as our `tfa2`
  driver and the wrong one wins the race. See AUDIO-1.
* **`CONFIG_MHI_BUS_DEBUG` should stay on.** It only adds the debugfs
  interface and enables no prints; see NOISE-3.

## 5. Check whether the board devicetree drifted

```bash
scripts/check-dts-drift.sh
```

The board DTS is a whole file, not a patch stack, which means a bump can
never give you a merge conflict in it — and also that an upstream
improvement is silently dropped. This diffs the upstream board DTS
against ours. The diff is always large (ours is the consolidated
description); read it for *new* upstream content, not for the size.

## 6. Build, flash, deploy, verify — unchanged

```bash
scripts/install-to-pmaports.sh --force
pmbootstrap build linux-postmarketos-qcom-sm8250 --force
pmbootstrap build device-oneplus-kebab --force
# phone into fastboot by hand, then:
pmbootstrap flasher flash_kernel && sudo fastboot reboot
# after it boots:
scripts/kebab-build.sh --deploy-only
```

Then on the device:

```bash
sudo sh verify-on-device.sh        # expect 26 passed, 0 failed
sudo sh check-kernel-drift.sh      # expect "no drift"
```

`--deploy-only` is not optional. `flash_kernel` writes `boot.img` only;
the modules stay on the old build until it runs. A module tree from the
previous kernel can load silently when the version string has not changed
— that is exactly how the stale `forcing TDM slot 0` line survived the
first r22 flash. See PACKAGING-2 and PACKAGING-3.

## 7. Re-check what a new kernel can quietly undo

A bump is the most likely way to lose a fix that lives outside the patch
series. Worth one `dmesg` grep each:

| check | expected | if it regresses |
|---|---|---|
| both amps on their own slot | `channel=0` and `channel=1` | AUDIO-3 |
| no DAPM widget collision | no `widget ... overwritten` | AUDIO-3 — the `sound-name-prefix` properties |
| DP link-training fallback | fallback still reached | DP-1/DP-2, patch 0014 |
| modem reaches mission mode | `Received EE event: MISSION MODE` | MODEM-1..MODEM-5 |
| Bluetooth has an address | not `00:00:00:00:00:00` | BT-1 |

`verify-on-device.sh` covers all five; this table is for when it fails and
you need to know which journal entry to read.

## 8. Update the journal

Record what went `REDUNDANT` (and therefore which patch numbers moved),
anything that needed rebasing, and any new noise.

If the patch count changes, `REFACTOR.md` states it in three places: the
summary table at the top, the `## The N kernel patches` heading, and the
list of upstreamable patches below that table.
