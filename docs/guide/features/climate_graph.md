# Climate Graph

The therapy-screen graph can show the humidifier climate instead of the flow
and pressure trace. It is meant for checking [Humid Start](humid_start.md): it
records the first hour of therapy (or the width you choose) and then stops, so
in the morning that stretch is still on the screen.

Enable it with `--patch-graph-climate y`. It requires `--patch-therapy-screen`
(on by default). You can't switch between the two graphs on the device: with
`--patch-graph-climate` every **+Graph** choice of the **Therapy scr.** setting
shows the climate chart, and the other choices show no graph, as before.

## What it shows

The chart fills the strip below the gauge where the trace is drawn, on both the
bar and the simple therapy screen. The **Graph** choice of **Therapy scr.**
gives it most of the screen instead: from below the title bar down to the
bottom divider, over the mode and pressure readouts and the pressure bar,
which are not drawn while it is shown. Time runs from left to right: 240 pixel
columns, each covering the chart width divided by 240, which is 15 seconds for
the default hour. A small tick at the bottom marks every 10 minutes (see
[Chart width](#chart-width)).

| Line | Colour | Scale |
|------|--------|-------|
| Plate (water) temperature, `HPT` | Red | Temperature |
| Tube temperature, `HTT` | Orange | Temperature |
| Estimated RH at the tube temperature | Blue | RH |
| Tube setting, `HTS` | Dark orange, dashed | Temperature |
| Humid Start Min Tube | Dark orange, dotted | Temperature |
| Target RH | Dark blue, dashed | RH |

- **Temperature scale:** 15 to 71 °C from bottom to top. The faint grey dotted
  lines are at 20, 30, 40, 50, 60 and 70 °C.
- **RH scale:** 20 to 100% from bottom to top. Values outside a scale are drawn
  at its edge.
- **No text:** the payload can't draw text on this screen, so the chart has no
  labels. With [Header Climate](general.md#header-climate) the title shows the
  current values in numbers.

The RH is the same estimate as the header climate readout and Humid Start use.
It is a guide to the trend, not a measurement.

The Min Tube line appears only with Humid Start installed, and
Min Tube (`--humid-start-min-tube`, a patch-time value) above 16 °C and below the
tube setting. Without a heated tube there is no tube line, tube setting or Min Tube.
If you change a setting, the dashed lines move within a column.

## Chart width

The chart covers 60 minutes by default. `--graph-width H:MM` sets another width,
from `0:10` to `12:00`, for example `--graph-width 8:00` for a whole night. Each
column covers the width divided by 240: 15 seconds for 1 hour, 2 minutes for
8 hours. The tick marks are every 10 minutes up to 2 hours, every 30 minutes up
to 6 hours and every hour beyond. The samples and the dashed setpoint lines
update once per column, so on a long chart a changed setting shows up after up
to a column's time.

With custom settings the width can also be changed on the device: a **Graph
Width (h)** setting, in hours from 0.2 to 12.0 in steps of 0.1 (6 minutes). It
is on the LCD page of the configuration menu when the backlight patch is
installed, otherwise in clinical **Options**. `--graph-width` sets its starting
value. Changing it clears the chart and starts it again from that moment.

The setting is the UART variable `RDT` and is saved with the other custom
settings.

## When it starts and stops

- **New night:** the chart clears when a new night starts: when the chart was
  not updated for 30 minutes (the chart is updated all through therapy, the
  screen off included, so only a stop of therapy leaves such a gap), or when
  the therapy timer goes back. A shorter stop, such as a visit to the toilet,
  keeps the chart. The therapy timer
  is not used to tell: it does not restart at zero every night and may count
  while therapy is off. Time is counted from the therapy timer, so the left
  edge is the start of therapy, also when the therapy screen was not shown at
  first.
- **Starting over by hand:** hold the Home button for 5 seconds during therapy
  to clear the chart and start it again from that moment. Without the climate
  chart, or with therapy off, the hold does what it did before.
- **When the chart is full:** after the chart width, sampling stops and the
  chart stays as it is until therapy stops.
- **Seeing it:** wake the screen with a button press while therapy is still
  running. When therapy stops the device leaves the therapy screen, and the
  chart is gone.
- **Repaint:** the samples are kept in memory, so the chart is redrawn after a
  menu or an alarm has covered the screen, or after leaving the therapy screen
  and coming back. Changing **Therapy scr.** to a different screen type while
  therapy runs rebuilds the page, which does not clear the chart.

`--patch-graph-keep-screen-on` keeps the screen on during therapy for the
**+Graph** choices.

## Not yet checked on a device

- **Colours:** they assume the display takes `0xBBGGRR` values, as the trace's
  colours suggest.
- **Screen off:** whether the therapy screen keeps updating while the screen is
  off. If it does not, the chart has gaps where it was off. The chart is kept
  across such a gap as long as the therapy timer kept running.
- **Therapy timer:** on a device it kept counting from the previous night
  instead of restarting at zero. A new night no longer depends on it, but it is
  not checked that the chart is updated all through therapy.
- **Strip size:** the simple therapy screen leaves a smaller strip than the bar
  screen; the chart scales to whatever strip it gets.
