# Patch series index

This file reflects the captured `APKBUILD` rather than trying to infer a cleaner upstream series. The numbering is historical and includes experiments, temporary diagnostics and reverts.

## Active `source=` order

| # | Patch | Area | Present |
|---:|---|---|:---:|
| 1 | `0001-kebab-enable-dsi-and-amb655x-panel.patch` | display / input / connectivity / early platform | yes |
| 2 | `0002-drm-panel-add-samsung-amb655x.patch` | display / input / connectivity / early platform | yes |
| 3 | `0003-kebab-touchscreen-s3908.patch` | display / input / connectivity / early platform | yes |
| 4 | `0004-input-add-synaptics-tcm-oncell.patch` | display / input / connectivity / early platform | yes |
| 5 | `0005-kebab-qca6390-wifi-bt.patch` | display / input / connectivity / early platform | yes |
| 6 | `0007-kebab-disable-unused-pcie1.patch` | display / input / connectivity / early platform | yes |
| 7 | `0008-kebab-drop-absent-hardware.patch` | display / input / connectivity / early platform | yes |
| 8 | `0009-kebab-wcd9385-audio.patch` | display / input / connectivity / early platform | yes |
| 9 | `0010-kebab-q6-dai-nodes.patch` | display / input / connectivity / early platform | yes |
| 10 | `0012-arm64-dts-qcom-oneplus-kebab-use-gic-mbi-for-pcie2.patch` | display / input / connectivity / early platform | yes |
| 11 | `0014-kebab-qca6390-local-bd-address.patch` | display / input / connectivity / early platform | yes |
| 12 | `0017-kebab-ramoops-pstore.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 13 | `0018-kebab-displayport-typec-and-dp.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 14 | `0019-kebab-displayport-usb2-host-role.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 15 | `0020-kebab-typec-vbus-supply.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 16 | `0021-kebab-displayport-four-lanes.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 17 | `0022-mhi-sdx55-fusion-sahara.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 18 | `0023-mhi-sahara-kebab-image-ids.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 19 | `0024-kebab-esoc-ap2mdm-gpio-hogs.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 20 | `0027-kebab-dpu-frame-done-timeout-bailout.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 21 | `0028-kebab-dp-link-bandwidth-margin.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 22 | `0029-kebab-dp-mode-valid-wide-bus.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 23 | `0026-kebab-mp2762-otg-regulator.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 24 | `0034-kebab-mp2762-drop-bad-pinctrl.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 25 | `0030-kebab-usb3-superspeed.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 26 | `0031-kebab-ramoops-ecc.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 27 | `0032-kebab-dpu-cap-four.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 28 | `0033-kebab-mp2762-as-vbus-supply.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 29 | `0036-kebab-source-pdo-1a5.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 30 | `0037-kebab-quiet-handover-irq.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 31 | `0038-kebab-dp-altmode-defer.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 32 | `0039-kebab-vbus-poll-timeout.patch` | USB-C / DisplayPort / charging / modem platform | yes |
| 33 | `0041-kebab-tfa9874-amp-nodes.patch` | audio | yes |
| 34 | `0042-kebab-tertiary-mi2s-audio.patch` | audio | yes |
| 35 | `0043-kebab-tfa2-codec-driver.patch` | audio | yes |
| 36 | `0044-kebab-tfa9872-alt-driver.patch` | audio | yes |
| 37 | `0045-kebab-tert-mi2s-pinctrl.patch` | audio | yes |
| 38 | `0046-kebab-tx-macro-debug-trace.patch` | audio | yes |
| 39 | `0047-kebab-q6afe-debug-trace.patch` | audio | yes |
| 40 | `0048-kebab-wcd938x-debug-trace.patch` | audio | yes |
| 41 | `0049-kebab-bob-micbias-headroom.patch` | audio | yes |
| 42 | `0051-kebab-wcd938x-amic-hpf-init.patch` | audio | yes |
| 43 | `0052-kebab-lpass-dmic-pins.patch` | audio | yes |
| 44 | `0053-kebab-va-dmic-capture.patch` | audio | yes |
| 45 | `0054-kebab-dmic-clock-2p4mhz.patch` | audio | yes |
| 46 | `0055-kebab-dmic23-pins.patch` | audio | yes |
| 47 | `0056-kebab-pm8008-camera-pmic.patch` | camera | yes |
| 48 | `0057-kebab-camss-camera-core.patch` | camera | yes |
| 49 | `0058-media-i2c-imx471-backport.patch` | camera | yes |
| 50 | `0059-kebab-front-camera-imx471-dt.patch` | camera | yes |
| 51 | `0060-kebab-front-vana-active-low.patch` | camera | yes |
| 52 | `0061-kebab-front-reset-active-high.patch` | camera | yes |
| 53 | `0062-kebab-pm8008-chip-enable-gpio93.patch` | camera | yes |
| 54 | `0063-kebab-front-reset-revert-active-low.patch` | camera | yes |
| 55 | `0064-kebab-imx471-temp-settle-clk-diag.patch` | camera | yes |
| 56 | `0066-kebab-vdig-1p1v-exact.patch` | camera | yes |
| 57 | `0068-kebab-revert-vdig-1p1v.patch` | camera | yes |
| 58 | `0069-kebab-imx471-powered-reset-pulse.patch` | camera | yes |
| 59 | `0070-kebab-imx471-3s-window-for-scan.patch` | camera | yes |
| 60 | `0071-kebab-revert-3s-window.patch` | camera | yes |
| 61 | `0072-kebab-cci1-pin-clock-rate-37500000.patch` | camera | yes |
| 62 | `0073-kebab-front-camera-move-to-cci1-master1.patch` | camera | yes |
| 63 | `0074-kebab-revert-cci1-master1-move.patch` | camera | yes |
| 64 | `0075-kebab-imx471-temp-full-bus-scan-while-powered.patch` | camera | yes |
| 65 | `0076-kebab-revert-imx471-temp-full-bus-scan.patch` | camera | yes |
| 66 | `0077-kebab-front-vana-active-high.patch` | camera | yes |
| 67 | `0078-kebab-cci1-i2c0-400khz.patch` | camera | yes |
| 68 | `0079-kebab-imx471-temp-rescan-vana-fixed-400khz.patch` | camera | yes |
| 69 | `0080-kebab-vdig-1p104v-nearest-step.patch` | camera | yes |
| 70 | `0081-kebab-revert-imx471-temp-rescan.patch` | camera | yes |
| 71 | `0082-kebab-imx471-vendor-power-sequence.patch` | camera | yes |
| 72 | `0083-kebab-front-camera-to-cci1-master1.patch` | camera | yes |
| 73 | `0084-kebab-imx471-temp-scan-master1.patch` | camera | yes |
| 74 | `0085-kebab-front-imx471-address-0x10.patch` | camera | yes |
| 75 | `0086-kebab-camss-csiphy-vdda-supplies.patch` | camera | yes |
| 76 | `0087-kebab-temp-diag-csiphy-settle.patch` | camera | yes |
| 77 | `0088-kebab-csiphy4-lane-numbering.patch` | camera | yes |
| 78 | `0089-kebab-revert-csiphy-diag.patch` | camera | yes |
| 79 | `0090-kebab-revert-imx471-scan-diag.patch` | camera | yes |
| 80 | `0091-kebab-sdx55-fusion-no-m3.patch` | SDX55 modem | yes |
| 81 | `0092-kebab-mhi-bind-efs-channel.patch` | SDX55 modem | yes |

## Active snapshot completeness

All patch files referenced by the captured active `source=` list are present, together with `config-postmarketos-qcom-sm8250.aarch64`. The follow-up collection closed the six kernel-aport gaps from the first archive.

The public copy of `0072` has only its local patch-author address sanitized; the corresponding `APKBUILD` checksum was updated.

## Present but not active in the captured `source=` list

- `0011-mhi-sdx55-qcom-profile.patch`
- `0015-kebab-displayport-altmode-usb-c.patch`
- `0025-kebab-usb-hsphy-tuning.patch`
- `0035-kebab-otg-vbus-ramp-delay.patch`
- `0040-kebab-otg-ilim-3a.patch`

These files are kept because they were present in the working aport directory and may explain the experiment history. Do not assume they should be re-enabled.

## Known cleanup points from the handoff

- Camera fixes to retain conceptually: `0077`, `0080`, `0082`, `0083`, `0085`, `0086`, `0088`; temporary diagnostics/reverts around them should be collapsed in a clean series.
- Modem fixes called out as important: `0012`, `0022`, `0023`, `0024`, `0037`, `0091`, `0092`.
- `0078` became a historical no-op for the front camera after the sensor moved to CCI1 master 1.
- `0073/0074` and `0075/0076` are documented as cancel-pairs.
- `0014` hard-codes a locally administered Bluetooth address as a bring-up workaround and needs a per-device solution for wider use.
