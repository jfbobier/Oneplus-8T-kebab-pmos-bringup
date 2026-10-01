# Research notes from the bring-up

These are the investigation notes from the original 81-patch bring-up,
kept because they record *why* this hardware needs what it needs — which
the refactored tree deliberately does not repeat. They are **historical**:
they describe the pre-refactor stack, and where they disagree with
`../REFACTOR.md` or `../REGRESSIONS.md`, those are right and these are
what was believed at the time.

| file | what it is |
|---|---|
| [MODEM.md](MODEM.md) | the SDX55 investigation, 62 KB. The most valuable document here: cellular data is still **not working** (MCFG aborts during apply, NV/IMEI lives in an `OEMNVBK` container rather than the EFS) and this is the full trail of what was tried. Start at §4.4. |
| [RESEARCH.md](RESEARCH.md) | general hardware findings across display, audio, camera, USB-C |
| [CAMERA.md](CAMERA.md) | IMX471 rails, CCI bus and slave address — the six root causes behind patches 0008/0009 |
| [MODEM-summary.md](MODEM-summary.md) | the short version of MODEM.md |
| [PATCH_SERIES.md](PATCH_SERIES.md) | what each of the original 81 patches did. The mapping to the current 18 is in [../REFACTOR.md](../REFACTOR.md). |
| [UPSTREAMING.md](UPSTREAMING.md) | what was considered upstreamable before the refactor |
| [REPRODUCIBILITY.md](REPRODUCIBILITY.md) | how the original archive was built |
| [PRIVACY.md](PRIVACY.md) | what was excluded from publication and why |
| [TECHNICAL_HANDOFF_2026-09-22.md](TECHNICAL_HANDOFF_2026-09-22.md) | the handoff written at the end of the bring-up |
| [ARCHIVE-README.md](ARCHIVE-README.md) | the original archive's own README, for its structure |

The archive's code — the 81 patches, its `userspace/` and its `tools/` —
is **not** here. It is superseded by the tree above, and keeping a
buildable copy would invite someone to build from it. Citations in the
other documents that begin `original/kernel-aport/` or
`original/userspace/` therefore point at files outside this repository;
they are kept so a claim can be traced to its source rather than asserted.
