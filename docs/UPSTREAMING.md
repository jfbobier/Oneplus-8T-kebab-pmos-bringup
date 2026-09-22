# Upstreaming / cleanup notes

This repository is intentionally a working bring-up archive. A submission-quality
series should be reconstructed from final behavior, not submitted wholesale.

## Camera

Good upstream candidates are the changes that describe actual board wiring and
required resources. Temporary scans, diagnostic traces and explicit revert
patches should disappear from a clean series.

The IMX471 backport/local power sequencing needs reconciliation with the current
upstream driver before submission. The board-specific distinction between
sensor-side lane numbering and CAMSS/CSIPHY-side lane positions should be
validated against the current binding and documented clearly.

## Bluetooth

`0014-kebab-qca6390-local-bd-address.patch` uses a hard-coded **locally
administered** address because the original per-device address was not recovered
during bring-up. That is useful for testing but is not a production multi-device
solution.

## Modem kernel side

`0091` is conceptually separable into profile policy (`no_m3`) and runtime-PM
behavior. It should be reviewed against current MHI power-management
expectations.

`0092` adds a Qualcomm EFS-specific WWAN port. The patch itself notes that the
abstraction is not generic enough to upstream as-is. It remains useful evidence
of the missing transport requirement.

## SDX55 userspace

The recovered source makes it possible to split generic fixes from kebab
integration:

- `tqftpserv-sdx55` source carries the SDX55 QRTR-instance/send-retry behavior;
- the kebab service file is separately board-specific (`modem_a`, firmware path,
  startup ordering);
- `pm_service_native.c` is byte-identical to the Apollo reference snapshot;
- `mhi_efs_sync.c` is included as GPL reference source from the Apollo package;
- kebab deliberately does not use the Apollo ESOC `mdm_helper_native` path.

That distinction is useful when deciding whether a change belongs in an upstream
utility, postmarketOS packaging, ModemManager rules, or only the kebab device
integration.

## Patch history cleanup

The active kernel `APKBUILD` includes several experiment/revert pairs from the
investigation. Before proposing anything upstream:

1. squash to the final board state;
2. remove temporary instrumentation and scans;
3. split independent subsystems;
4. preserve factual commit-message evidence for hardware wiring/power
   sequencing;
5. rebuild and retest from a clean tree rather than the accumulated experiment
   stack.
