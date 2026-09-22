# Privacy and publication review

This repository was prepared specifically for public sharing.

## Not included

The release bundle contains no intentional copy of:

- IMEI, ICCID, IMSI, phone number or SIM PIN value;
- EFS/NV dumps or RF-calibration data;
- raw DIAG/QMDL captures;
- modem/vendor firmware images or Android partition images;
- passwords, API tokens, SSH private keys or other credentials;
- private builder host/address information from the original handoff;
- the device-local IMEI restoration helper/service found in the phone-side
  collection;
- the phone's locally built ELF binaries.

The exact userspace file `/etc/kebab-sim-pin` is **not** included. During the
phone-side review, a local helper containing the factory IMEI as a literal was
detected and excluded in full rather than merely masking the number.

## Builder-source review

The later builder-side source collection was used only to recover public source,
APKBUILDs and provenance. Raw metadata containing local `/home/...` paths was not
carried into the repository. Instead, only the relevant public commit IDs,
package relationships and source hashes are summarized in
`userspace/PROVENANCE.md`.

The recovered local package/source trees contain no detected long subscriber or
device identifiers, private-key markers, known local builder address, or local
builder email/hostname remnants.

## Sanitizations applied

- local patch-author placeholders were replaced with `kebab bring-up
  <local@invalid>`; the `APKBUILD` checksum for sanitized patch `0072` was
  updated accordingly;
- helper scripts default to the generic postmarketOS login name `user` rather
  than a local username;
- the archival handoff replaces private builder details with placeholders;
- carrier-identifying text in the archival handoff was generalized;
- public maintainer/copyright addresses already present in imported/open-source
  code are retained for attribution.

## Bluetooth address note

Patch `0014` contains a hard-coded locally administered Bluetooth address used as
a bring-up workaround. The patch text explicitly states that this is **not the
device's original address**. It is therefore not treated as a leaked device
identifier, but it should not be used as the final multi-device solution.

## Before adding logs

Many otherwise useful modem commands print unique subscriber/device identifiers.
Redact logs before committing them. A conservative first pass is to mask long
decimal identifiers, then inspect manually.

Run `./scripts/privacy-scan.sh` from the repository root before publication.
