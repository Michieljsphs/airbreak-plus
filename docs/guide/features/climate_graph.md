# Climate Graph

The therapy graph can show the humidifier climate instead of pressure and
flow. It is meant for checking [Humid Start](humid_start.md): it records the
first 60 minutes of therapy and then stops, so in the morning that hour
is still on the screen.

Choose the graph when patching. Tick **patch-graph-climate** in the patch
options window, or use `--patch-graph-climate y` (`PATCH_GRAPH_CLIMATE=1`). It
needs the graph patch (`--patch-fw-graph`). Without it the graph shows
pressure and flow as before. You can't switch between the two on the device.

## Chart width

The chart covers 60 minutes by default. `--graph-width H:MM` sets another
width, from `0:10` to `12:00`, for example `--graph-width 8:00` for a whole
night. The chart is always 240 pixel columns wide, so each column covers the
width divided by 240: 15 seconds for 1 hour, 2 minutes for 8 hours. The tick
marks at the bottom are every 10 minutes up to 2 hours, every 30 minutes up
to 6 hours and every hour beyond. The dashed setpoint lines and the samples
update once per column, so on a long chart a changed setting shows up after up
to a column's time. The chart stops sampling when it is full.

## What it shows

The chart is much taller than the pressure graph: it fills the therapy screen
from just below the title bar down to the bottom divider (y 74 to 278), over
the mode and pressure readouts (such as VAuto, PS 4.0 and the two pressures)
and the large humidifier icon. The chart is redrawn completely every 15
seconds, so if one of those stock elements repaints itself, it is covered
again within a few seconds. Time runs from left to
right: one pixel column every 15 seconds, so the 240 columns cover 60
minutes (see [Chart width](#chart-width)). A small tick at the bottom marks
every 10 minutes.

| Line | Colour | Scale |
|------|--------|-------|
| Plate (water) temperature, `HPT` | Red | Temperature |
| Tube temperature, `HTT` | Orange | Temperature |
| Estimated RH at the tube temperature | Blue | RH |
| Tube setting, `HTS` | Dark orange, dashed | Temperature |
| Humid Start Min Tube | Dark orange, dotted | Temperature |
| Target RH | Dark blue, dashed | RH |

- **Temperature scale:** 15 to 71 °C from bottom to top, about 0.4 °C per
  pixel. The faint grey dotted lines are at 20, 30, 40, 50, 60 and 70 °C.
- **RH scale:** 20 to 100% from bottom to top, about 0.55% per pixel. Values
  outside a scale are drawn at its edge.
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

- **New session:** the chart clears when therapy starts. Time is counted
  from the therapy timer, so the left edge is always the start of therapy,
  also when the therapy screen or Monitoring was off at first; the minutes
  before the chart was first shown stay empty. The therapy timer does not
  restart at zero every night, so the chart takes a new night to start when
  therapy has been off for more than 30 minutes: the chart was not updated
  for 30 minutes while the therapy timer advanced less than 5 minutes. The
  chart also clears if the timer goes back. Show the therapy screen within 5 minutes of starting therapy, or the
  new night is taken as the old one going on.
- **Starting over by hand:** hold the Home button for 5 seconds during
  therapy to clear the chart and start it again from that moment. Without the
  climate chart, or with therapy off, the hold does what it did before.
- **When the chart is full:** after 60 minutes, or the chosen width, sampling stops and the chart stays as it is until
  therapy stops.
- **Seeing it:** wake the screen with a button press while therapy is still
  running. When therapy stops the device leaves the therapy screen, and the
  chart is gone.
- **Repaint:** the samples are kept in memory, so the chart is redrawn after
  a menu or an alarm has covered the screen, or after leaving the therapy
  screen and coming back. When the screen is repainted, the readouts and the
  humidifier icon may flash for a moment before the chart covers them again.

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
- **Therapy timer:** that it counts in 10 ms steps only while therapy runs.
  On a device it kept counting from the previous night instead of restarting
  at zero, so the chart takes the start of each session from the timer's
  value then. If it turns out to count while therapy is off as well, a new
  night is not recognised and the old chart stays.
