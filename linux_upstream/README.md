# Upstreaming the DisplayPort work to mainline Linux

Six patches from the OnePlus 8T (kebab) bring-up that are **not
kebab-specific** and fix bugs still present in Linus's tree. Prepared for
a dedicated session; nothing here has been sent to anyone.

The device bring-up itself — the board devicetree and the six new drivers
— is deliberately **not** part of this. That belongs to the
postmarketOS/Nura sm8250 tree, and their AI policy makes it a non-starter
without redoing the work by hand. Mainline is a separate project with its
own rules; these six are bug fixes to code that already exists there.

---

## Scope: this directory is self-contained

This is a **separate project** from the device bring-up around it, and
the boundary matters.

* The patches here are **copies**. The originals live in `../kernel/` and
  are the configuration running on a working phone.
* **Do not edit anything outside this directory** while upstreaming.
  Rebasing, rewriting commit messages, splitting series, responding to
  review — all of it happens here or in a scratch kernel clone, never in
  `../kernel/`. That tree is a known-good device configuration; changing
  it to suit a mailing list would break a working phone for no benefit.
* **Nothing here needs the build host.** No cross-compilation of the
  kebab kernel, no flashing, no device. Upstreaming needs a mainline (or
  `msm-next`) checkout, `checkpatch.pl`, `get_maintainer.pl` and
  `git send-email`. A workstation is enough.
* Divergence between the two is expected and fine. If a patch is reshaped
  for review it does **not** need back-porting into `../kernel/` — the
  device is already running the version that works.

The one thing worth copying *back*, if it ever happens: a note in
`../docs/REGRESSIONS.md` recording that a patch went upstream, was
rejected, or was superseded. That is history worth keeping; the code is
not.

---

## Before anything else: the sign-off is yours

Every patch here has **no `Signed-off-by:` line**, deliberately.

Mainline requires one, and it is not a formality — it is the Developer's
Certificate of Origin, a legal attestation that you have the right to
submit the work and that you stand behind it. These patches were produced
with substantial AI assistance. That attestation is yours to make or
decline, and nobody can make it for you.

Two practical consequences:

* **Check current kernel policy on AI-assisted contributions before
  sending.** It is evolving. The kernel has no blanket prohibition of the
  kind Nura has, but norms differ by subsystem and maintainer.
* **Review is the work.** Sending is ten minutes; defending the reasoning
  in a thread with Dmitry Baryshkov over several weeks is the actual
  commitment. The good news is the reasoning is written down and is
  defensible — see "Evidence" below.

---

## The patches

All six verified against `torvalds/linux` on **2026-10-04**. Line numbers
are from that check.

| # | What | Still broken in mainline? |
|---|---|---|
| 0014 | DP link-training fallback is unreachable on Type-C boards | yes — `msm_dp_aux_is_link_connected()` still guards both branches, `dp_ctrl.c:2382` and `:2407` |
| 0015 | NULL deref reading the DP test debugfs files | yes — `connector->status` dereferenced unchecked at `dp_debug.c:95, 116, 147, 173` |
| 0021 | Modes sized on the sink's claim, not the trained link | yes — `supported_rate_khz = link_info->num_lanes * link_info->rate * 8` in `msm_dp_display_mode_valid()` |
| 0020 | A healthy DP attach logged as nine errors | yes — `DRM_ERROR_RATELIMITED` at `dp_ctrl.c:1490`, `DRM_ERROR` at `:1637 :1644 :1701`, and `dp_aux.c:461` |
| 0019 | DP/HDMI sink audio configured too late for a stateful CPU DAI | yes — `drm_connector_hdmi_audio_ops` still has `.prepare` and no `.hw_params` |
| 0005 | DP altmode permanently rejected when the port is not yet DFP | yes — still `return -EPROTO` at `displayport.c:771` |

### Lead with 0014

It is the strongest of the six. DP link-training fallback — the rate and
lane-count step-down — is **dead code on every Type-C DP board using this
driver**, because the retry loop gates itself on the DP controller's own
HPD register, which reads DISCONNECTED when HPD arrives out of band
through a `drm_dp_hpd_bridge`. Four lines.

It is latent, which is why it survived: fallback only runs when training
fails at full rate, and with a good cable it never runs at all. It was
found with a cable whose lanes 2 and 3 never achieve clock recovery.

This is not phone-only. The same driver serves the Snapdragon X Elite
(`x1e80100`) laptops, which are all Type-C DP.

### 0015 is a crash and can go on its own

Anything that reads `dp_test_active`, `dp_test_data` or `dp_test_type`
under `/sys/kernel/debug/dri/*/DP-*/` oopses the kernel on external DP.
`msm_dp_debug_init()` stores a connector pointer that is NULL at that
point and nothing checks it. Trivially triggered — a shell glob did it
here three times. Likely wants `Fixes:` and `Cc: stable`.

### 0021 is deliberately half a fix

It makes the mode list reflect the trained link. It does **not** make
userspace re-probe after a fallback, which needs the DRM link-status
property — and that cannot be set from `msm_dp_bridge_atomic_enable()`
because the modeset locks are held there and
`drm_connector_set_link_status_property()` takes `connection_mutex`.
That is what the existing upstream

```c
// TODO: schedule drm_connector_set_link_status_property()
```

refers to. Expect a reviewer to ask for the complete fix. Deciding
whether to do the deferred-work half *before* sending is probably wiser
than being asked for it in review.

### 0019 will attract design feedback

It adds `.hw_params` to the DRM HDMI audio helper, gated behind a new
`drm_bridge.hdmi_audio_prepare_early` flag so the other seven users of
that helper are untouched. The underlying problem is real — ASoC walks
CPU DAIs before codec DAIs and aborts on the first error, so a CPU DAI
that needs the sink's audio clock can never get it when all the sink
configuration lives in `.prepare`.

But the shape is arguable. A maintainer may prefer moving `.prepare` to
`.hw_params` for everyone, or fixing it in `hdmi-codec.c` instead. Go in
expecting to negotiate the design rather than defend the diagnosis.

Note one side effect to disclose: providing `.hw_params` at all means
`hdmi_codec_hw_params()` stops returning early for these connectors, so
it now runs `hdmi_codec_fill_codec_params()` and the IEC958 fill before
the (inert) callback. Both compute into an on-stack struct that is
discarded, and the one shared value written, `daifmt->bit_fmt`, is set by
`hdmi_codec_prepare()` to the identical value immediately after.

---

## Dropped on purpose

`0004-drm-msm-dp-don-t-apply-the-wide-bus-pixel-clock-halv.patch` is
**already fixed in mainline** and is not in this directory. Linus's tree
already separates the link bandwidth clock from the DPU datapath clock:

```c
link_pclk_khz = is_yuv_420 ? mode_pclk_khz / 2 : mode_pclk_khz;
if (is_yuv_420 || msm_dp_display->wide_bus_supported)
        mode_pclk_khz /= 2;
...
mode_rate_khz = link_pclk_khz * mode_bpp;
```

Sending it would have been a duplicate.

---

## Rebasing: these do not apply to mainline as-is

The patches are against postmarketOS's `linux-postmarketos-qcom-sm8250`
7.2.0 fork, which already carries msm DP work that has **not** landed in
Linus's tree. The clearest symptom: our tree has
`msm_dp_bridge_mode_valid()` as a `drm_bridge_funcs` callback, while
mainline still has `msm_dp_display_mode_valid()` taking a `struct msm_dp
*`. 0021 in particular has to be re-written against whichever base you
target, not just rebased.

Target the msm tree rather than Linus directly:

```
T: git https://gitlab.freedesktop.org/drm/msm.git
```

and work on its `msm-next` branch. `scripts/try-patches.sh` from the
parent repository, pointed at a checkout of that branch, reports each
patch as `APPLIES`, `FUZZ`, `CONFLICT` or `REDUNDANT` in one run — start
there.

---

## Where to send what

**drm/msm — 0014, 0015, 0020, 0021**

```
M: Rob Clark <robin.clark@oss.qualcomm.com>
M: Dmitry Baryshkov <lumag@kernel.org>
R: Abhinav Kumar <abhinav.kumar@linux.dev>
R: Jessica Zhang <jesszhan0024@gmail.com>
R: Sean Paul <sean@poorly.run>
R: Marijn Suijten <marijn.suijten@somainline.org>
L: linux-arm-msm@vger.kernel.org
L: dri-devel@lists.freedesktop.org
L: freedreno@lists.freedesktop.org
```

**drm/display — 0019.** Same dri-devel list, but the HDMI audio helper
has its own history; run `get_maintainer.pl` on the actual diff rather
than assuming the msm reviewers.

**usb/typec — 0005.** Different subsystem entirely: `linux-usb@vger.kernel.org`,
Greg Kroah-Hartman and Heikki Krogerus. Do not fold it into a DRM series.

Suggested order: **0015** alone first (self-contained crash fix, good way
to learn the process), then **0014**, then **0021** once you have decided
about its second half, then **0020**, then **0019** and **0005** on their
own timelines.

## Mechanics

```bash
scripts/checkpatch.pl --strict <patch>     # must be clean
scripts/get_maintainer.pl <patch>          # generates the Cc list
git send-email --to=... --cc=... <patch>
```

One logical change per patch — these already are. Add `Fixes:` where you
can identify the introducing commit, and `Cc: stable@vger.kernel.org` for
0015. Expect v2, v3; version the series and include a changelog under the
`---` line.

---

## Evidence

The reasoning behind each patch, with the hardware captures, is in the
parent repository's `docs/REGRESSIONS.md`:

| patch | journal entry | what the evidence is |
|---|---|---|
| 0014, 0021 | `DP-1 / DP-2` | DPCD 0x202/0x203 read at each voltage-swing step, showing lanes 2 and 3 never achieve CR at any rate; the full fallback sequence; 4K@60 working once the link trained at full width |
| 0015 | `DP-1 / DP-2` | three reproduced oopses, and why `timeout 3 cat` does not help when the fault is inside the read |
| 0019 | `AUDIO-DP` | ftrace showing `msm_dp_audio_prepare()` is never called, only `msm_dp_audio_shutdown()` from teardown; the ADSP answering `-110` then `ADSP_EALREADY` |
| 0020 | `NOISE-2` | 11 error lines → 0 on a successful attach, measured across two kernels with a monitor attached |
| 0005 | `USB-1`, `DP-3` | Type-C role and altmode state at the point of failure |

All of it was taken on real hardware. If a reviewer asks "how do you
know", that file is the answer.
