# Suggested sharing text

## postmarketOS Matrix

I have published my current OnePlus 8T (`kebab`) SM8250 bring-up work from a
postmarketOS/Linux 7.2 baseline. The repo contains the complete local kernel
aport snapshot, a sanitized technical handoff, the IMX471 front-camera
root-cause write-up, and the SDX55 modem/userspace findings.

The front camera is working. The SDX55 now boots reliably to mission mode and
stays up; SIM/PIN works in my tested installation. The repository also contains
the public-safe EFS/ModemManager/UIM configuration plus the recovered source
packages for `pm_service_native`, `tqftpserv-sdx55`, and the GPL source used for
`mhi_efs_sync`. Cellular radio/data is still blocked in the MCFG apply path, so
this is a working research baseline rather than an upstream-ready series.

I would appreciate review from anyone working on `kebab`, SM8250 camera support,
or SDX55 fusion devices. In particular, the camera findings, the SDX55 M3/EFS
behavior, and the tqftpserv QRTR/send handling may be reusable on related
devices.

## Message to an SM8250 maintainer

Hi — I have put together a public handoff of my OnePlus 8T (`kebab`) work on the
postmarketOS SM8250 7.2 baseline. I kept the accumulated patch series for
traceability but documented which parts are experiments versus final fixes.

The strongest results are a working IMX471 front camera after fixing six stacked
board/integration issues, and a stable SDX55 mission-mode boot after addressing
M3/runtime-PM behavior and the missing MHI EFS transport. The matching userspace
source/configuration is included as well. Radio/data is not solved yet; the
remaining failure is localized to MCFG aborting after stack deactivation during
config apply.

If useful, I would especially value feedback on which DT/camera pieces are worth
splitting for upstream, on a better upstream model for the SDX55 EFS channel than
the local `WWAN_PORT_EFS` workaround, and on whether the tqftpserv instance/send
fixes should be proposed separately from the kebab-specific service packaging.
