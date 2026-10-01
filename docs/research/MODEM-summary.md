# SDX55 modem findings

## Current state

The external SDX55 reaches mission mode and remains stable on the tested
baseline. The remaining blocker is the radio/configuration path, not the basic
MHI/Sahara boot chain.

Observed high-level sequence:

```text
BHI -> SDX55 secondary bootloader -> Sahara image transfer -> mission mode
```

Mission mode exposes DIAG/EFS/MBIM/QMI and the IP channels used by the tested
userspace.

## Stability fix: patch 0091

`0091-kebab-sdx55-fusion-no-m3.patch` does two related things for the SDX55
fusion profile:

1. marks the profile `no_m3 = true`;
2. prevents `pm_runtime_allow()` from re-enabling autosuspend on mission-mode
   entry for that profile.

The motivation recorded during testing was repeated resume failure after
M3/autosuspend cycling. Keeping this modem out of M3 removed that failure mode.

## RMTEFS / EFS fix: patch 0092

The modem firmware behaves as a flashless RMTEFS build and expects the
application processor to service its filesystem over the MHI EFS channel. Before
the channel was bound, the first EFS sync could not complete and the modem later
hit a repeatable ERRFATAL.

`0092-kebab-mhi-bind-efs-channel.patch` introduces `WWAN_PORT_EFS` and binds the
MHI `EFS` channel in `mhi_wwan_ctrl`, creating a userspace-visible EFS port.

This patch is deliberately described as **not upstreamable as-is**: EFS is
Qualcomm-specific and not a generic WWAN control-port abstraction.

## Userspace required by the working setup

The public-safe phone configuration and recovered builder sources are under
[`../userspace/`](../userspace/). The important pieces are:

- `mhi-efs-sync.service` plus recovered GPL `mhi_efs_sync.c` source;
- local `tqftpserv-sdx55` APKBUILD/source package;
- local `kebab-modem-tools` package containing `pm_service_native`;
- `77-mm-ignore-sdx55-efs.rules`;
- `77-mm-sdx55-fusion.rules`;
- guarded UIM provisioning/PIN automation.

The EFS-ignore rule is important: without it, ModemManager can probe the EFS
transport as though it were a control port, which was observed to destabilize
the modem.

The builder comparison showed that `pm_service_native`, `diag_reader`, and the
tqftpserv source files are byte-identical to the captured Apollo package
versions. The kebab-specific differences are mainly package selection and
service/device layout. See `userspace/PROVENANCE.md`.

## tqftpserv / remote filesystem

The local `tqftpserv-sdx55` package describes two SDX55-relevant source changes:

1. publish QRTR service 4096 on **instance 3**, which is what this PCIe-attached
   SDX55 looks up;
2. retry transient `send()` failures instead of letting a large windowed
   transfer stall.

The kebab service then adapts the firmware mount to the device's A/B `modem_a`
partition.

## SIM/PIN findings

The tested installation reached:

```text
Card state: present
Application state: ready
PIN1 state: enabled-verified
```

Two issues were distinguished during debugging:

- restoring certain policy-manager files could stall MCFG/UIM state handling;
- automatic subscription provisioning records were absent, so the SIM
  application needed explicit activation before ModemManager startup.

The guarded UIM/PIN automation is included as
`userspace/systemd/kebab-uim-provision.service` plus
`userspace/bin/kebab-uim-provision.sh`. **No PIN value is included.** The script
reads a local root-only `/etc/kebab-sim-pin` when present and refuses automatic
verification unless all three PIN1 retries remain.

An earlier script comment linked PIN timing to the radio failure. Later testing
falsified that causal chain: the card can be ready before MCFG's later pass and
the radio failure remains unchanged. UIM provisioning/PIN automation is a
working feature, not the fix for MCFG/radio.

## Remaining blocker: MCFG apply

Configuration **selection and transfer work**. The modem resolves a profile and
downloads it completely. The failure occurs after MCFG asks for protocol-stack
deactivation: it returns to idle instead of completing apply/reactivation.

Resulting symptoms include an offline DMS state, empty network/RAT capability
reporting and NAS not being provisioned.

The next work should therefore focus on the MCFG state transition/apply path,
rather than repeating Sahara, EFS transport or basic SIM-detection debugging.

## High-value next comparisons

The original handoff recommends comparing this state with another SM8250 + SDX55
fusion device known to have working mobile data, especially:

- `/nv/item_files/mcfg/` contents;
- whether an `mcfg_sw` profile is shipped/activated;
- automatic MCFG selection settings;
- interpretation of the state-machine input that causes the apply path to return
  to idle.

Do not publish raw EFS/NV content when doing that comparison. Extract only the
minimum non-unique state needed to describe the difference.
