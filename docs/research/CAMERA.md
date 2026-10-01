# IMX471 front camera bring-up

## Result

On the tested OnePlus 8T (`kebab`) baseline, the front IMX471 probes, builds a complete media graph, receives CSI data and captures frames.

Observed media path:

```text
imx471 -> csiphy4 -> csid0 -> vfe0_rdi0 -> /dev/video0
```

Readback while streaming showed the expected IMX471 chip ID, streaming enabled, four-lane mode and RAW10 output.

## Six stacked root causes

| # | Patch | Finding |
|---|---|---|
| 1 | `0077` | VANA load-switch GPIO polarity was wrong. The rail is active-high. |
| 2 | `0080` | PM8008 LDO constraints needed a representable 8 mV step; `1104000 uV` works where exact `1100000 uV` does not. |
| 3 | `0082` | The driver needed the board/vendor rail sequence rather than leaving VIO/VDIG permanently on. |
| 4 | `0083` | Front camera belongs on CCI1 master 1, not master 0. |
| 5 | `0085` | Actual sensor address is `0x10`, not the assumed `0x1a`. |
| 6 | `0086` + `0088` | CAMSS needed CSIPHY supplies and the CAMSS endpoint needed clock lane 7 plus 0-based data-lane positions. |

The earlier “likely hardware fault” conclusion was therefore wrong: multiple software faults invalidated the diagnostic evidence used to reach it.

## Power sequencing

The vendor sensor-module descriptor indicated this power-up order:

```text
VANA -> VDIG -> VIO -> MCLK(19.2 MHz) -> RESET
```

with the reverse order on shutdown. This was used as the basis for `0082`.

## CCI and lane mapping

The working front-camera bus is CCI1 master 1, corresponding to the second CCI1 I2C master in the tested system.

The sensor-side endpoint remains four lanes using the sensor driver's expected numbering. The CAMSS/CSIPHY side uses:

```dts
clock-lanes = <7>;
data-lanes = <0 1 2 3>;
bus-type = <MEDIA_BUS_TYPE_CSI2_DPHY>;
```

The crucial distinction is that the CSIPHY implementation maps data-lane positions directly when building its enable mask; using `1 2 3 4` there leaves physical lane 0 disabled.

## Manual capture recipe

```sh
M=/dev/media0
for E in '"imx471 24-0010":0' '"msm_csiphy4":0' '"msm_csiphy4":1' \
         '"msm_csid0":0' '"msm_csid0":1' '"msm_vfe0_rdi0":0' '"msm_vfe0_rdi0":1'; do
    media-ctl -d "$M" -V "$E [fmt:SRGGB10_1X10/1928x1088]"
done

v4l2-ctl -d /dev/video0 \
    --set-fmt-video=width=1928,height=1088,pixelformat=pRAA
v4l2-ctl -d /dev/video0 \
    --stream-mmap=4 --stream-count=10 --stream-to=/tmp/f.raw
```

The tested driver exposes a single 1928x1088 RAW10 mode. Image quality/tuning was not completed; frame capture working is distinct from libcamera tuning.

## Rear-camera map recovered during the work

| Sensor | Role | CCI master | CSIPHY |
|---|---|---:|---:|
| IMX586 | wide/main | CCI0 master 0 | 0 |
| IMX481 | ultrawide | CCI0 master 1 | 1 |
| GC02M1B | mono | CCI0 master 1 | 2 |
| GC5035 | macro | CCI1 master 0 | 3 |
| IMX471 | front | CCI1 master 1 | 4 |

Rear sensor slave addresses were not established in this handoff. The key lesson from the IMX471 work is not to assume family-default I2C addresses; discover them while the sensor is actually powered.

For the full investigation history, including failed hypotheses and vendor descriptor notes, see [`archive/TECHNICAL_HANDOFF_2026-09-22.md`](archive/TECHNICAL_HANDOFF_2026-09-22.md).
