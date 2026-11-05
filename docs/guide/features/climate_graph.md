# Climate Graph

The therapy graph can show the humidifier climate instead of pressure and
flow. It is meant for checking [Humid Start](humid_start.md): it records the
first hour of therapy and then stops, so in the morning that hour is still on
the screen.

Choose the graph when patching. Tick **patch-graph-climate** in the patch
options window, or use `--patch-graph-climate y` (`PATCH_GRAPH_CLIMATE=1`). It
needs the graph patch (`--patch-fw-graph`). Without it the graph shows
pressure and flow as before. You can't switch between the two on the device.

## What it shows

The chart uses the same strip below the pressure gauge as the pressure graph.
Time runs from left to right: one pixel column every 15 seconds, so the 240
columns cover 60 minutes. A small tick at the bottom marks every 15 minutes.

| Line | Colour | Scale |
|------|--------|-------|
| Plate (water) temperature, `HPT` | Red | Temperature |
| Tube temperature, `HTT` | Orange | Temperature |
| Estimated RH at the tube temperature | Blue | RH |
| Tube setting, `HTS` | Dark orange, dashed | Temperature |
| Humid Start Min Tube | Dark orange, dotted | Temperature |
| Target RH | Dark blue, dashed | RH |

- **Temperature scale:** 15 to 71 °C from bottom to top, 0.7 °C per pixel. The
  faint grey dotted lines are at 20, 30, 40, 50, 60 and 70 °C.
- **RH scale:** 20 to 100% from bottom to top, 1% per pixel. Values outside a
  scale are drawn at its edge.
- **No text:** the payload can't draw text on this screen, so the chart has no
  labels. With [Header Climate](general.md#header-climate) the title shows the
  current values in numbers.

The RH is the same estimate as the header climate readout and Humid Start
use. It is a guide to the trend, not a measurement.

The Min Tube line appears only with the Humid Start Min Tube setting in the
menu, Humid Start switched on, and Min Tube above 16 °C and below the tube
setting. Without a heated tube there is no tube line, tube setting or Min
Tube. If you change a setting, the dashed lines move within 15 seconds.

## When it starts and stops

- **New session:** the chart clears when therapy starts.
- **After 60 minutes:** sampling stops and the chart stays as it is until
  therapy stops.
- **Seeing it:** wake the screen with a button press while therapy is still
  running. When therapy stops the device leaves the therapy screen, and the
  chart is gone.
- **Repaint:** the samples are kept in memory, so the chart is redrawn after
  a menu or an alarm has covered the screen.

`--patch-graph-keep-screen-on` keeps the screen on during therapy for either
graph.

With custom settings, **Monitoring** in clinical Options switches the graph
on and off, as for the pressure graph.

## Not yet checked on a device

- **Colours:** they assume the display takes `0xBBGGRR` values, as the
  pressure graph's colours suggest.
- **Screen off:** whether the therapy screen keeps updating while the screen
  is off. If it does not, the chart has gaps where it was off. The chart is
  kept across such a gap as long as the therapy timer kept running.
