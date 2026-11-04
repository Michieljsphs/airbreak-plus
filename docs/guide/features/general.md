# General Features

These features expand the modes, settings, recording, and hardware options
available on supported AirSense 10 and AirCurve 10 firmware. They are included
in the standard patched images unless noted otherwise.

## All Therapy Modes

AirSense 10 and AirCurve 10 variants from the same firmware generation share
the underlying therapy implementations. 
The device-specific configuration
normally limits the mode selector to the modes sold with a particular model.
The patch removes that restriction, exposing CPAP, AutoSet, APAP, S, ST, T,
VAuto, ASV, ASVAuto, iVAPS, PAC, and AutoSet for Her.

<p align="center">
  <img src="../../images/unlocked-therapy-modes-1.png" alt="Unlocked therapy modes, first page" width="220">
  <img src="../../images/unlocked-therapy-modes-2.png" alt="Unlocked therapy modes, second page" width="220">
</p>

This patch changes only mode availability. It does not expose the related
clinical settings; that is handled by the clinical settings patch below.
It also leaves the model's original EDF signal selection unchanged. Full recording
for the additional modes requires the EDF merge patch.

## Clinical Settings and Pressure Range

The clinical menu exposes the settings associated with the unlocked therapy
modes, including mode-specific pressure, timing, trigger, cycle, and comfort
controls.
The menu continues to show only settings relevant to the currently
selected mode.

Standard pressure controls are expanded to the full 1.0 to 30.0 cmH2O range.

<p align="center">
  <img src="../../images/unlocked-pressure-ranges.png" alt="Unlocked ASVAuto pressure ranges" width="240">
</p>

## Fall Time

This optional patch adjusts the stock pressure transition from IPAP to EPAP
in S, ST, T, PAC, and iVAPS. Longer times make the pressure fall more gradually.
S/ST/T/PAC use a linear fall; iVAPS retains its curved pressure transition.
In S, turn Easy-Breathe Off to use Fall Time.

With custom settings, clinical **Therapy** contains:

| Setting | UART | Values | Default |
|---------|------|--------|---------|
| Fall Time | RCW | 100-900 ms, step 50 ms | 100 ms |

Without custom settings, the stock fall remains unchanged.
In S/ST/T/PAC, [Square Wave](squarewave.md) supplies its own pressure
waveform; turn it Off to use Fall Time.

## ST iBR

This optional patch adds adaptive backup timing in ST. With **iBR Off**, the
existing **Backup Rate** (`RRT`) sets the fixed backup frequency. With **iBR On**,
the same setting supplies the target rate for adaptive backup.

After a spontaneous breath, the machine allows up to 1.5 times the target breath
period before delivering a timed breath. Successive timed breaths shorten this
period toward the target: approximately 1.5, 1.375, 1.25, 1.125, then 1 times the
target period. A spontaneous trigger restarts this sequence.

With custom settings, clinical **Therapy** contains:

| Setting | UART | Values | Default |
|---------|------|--------|---------|
| iBR | RXW | Off, On | Off |

Without custom settings, ST retains its stock fixed backup rate.

## iVAPS AutoEPAP

This optional patch adds Lumis-style automatic EPAP adjustment to iVAPS.
It raises EPAP in response to inspiratory flow shape and sustained
reductions in flow, then gradually lowers it when those responses subside.
The existing iVAPS controller continues to adjust pressure support toward
Target Va.

With custom settings, clinical **Therapy** contains:

| Setting | UART | Values | Default |
|---------|------|--------|---------|
| AutoEPAP | RSA | Off, On | Off |
| Min EPAP | RDF | iVAPS EPAP range, step 0.2 cmH2O | 5.0 cmH2O |
| Max EPAP | RDM | iVAPS EPAP range, step 0.2 cmH2O | 15.0 cmH2O |

With AutoEPAP On, Min/Max EPAP replace the fixed EPAP setting in the menu.
If Fall Time is installed, flow-shape analysis uses its selected duration.

## Target Humidity

This patch lets users adjust humidification above or below the stock
85% relative humidity target, in both Auto and Manual Climate Control.
Lower targets reduce the requested moisture; higher targets increase it.
Manual humidity levels remain available and work alongside this setting.

With custom settings, **Target RH (%)** appears in clinical **Accessories**:
70-100%, in steps of 1 percentage point. The setting is saved across restarts
and is also available over UART as `RCH`. The default is 85%, unless a different
value is selected when building the image.

Without custom settings, the target stays at the value selected when building.
Without this patch, the stock 85% target remains unchanged.

The percentage is a target, not a measurement of humidity at the mask.
Higher targets increase condensation risk; 100% targets saturation.

## Tube Warm-up

At the start of therapy, the heated tube normally reaches its temperature
within minutes, while the humidifier plate takes much longer to heat the water.
Until then the air is warm but dry. This optional patch (SX567 only) holds
the tube back while the humidifier warms up.

During warm-up, the tube setpoint is capped at the temperature where the
estimated humidity output gives the target RH (85%, or the
[Target Humidity](#target-humidity) setting). The humidity model keeps aiming
for the full tube temperature, so the plate heats at full demand. The cap
follows the plate up and ends when it reaches the tube temperature setting,
when the plate stops rising (less than 0.2 °C per minute), or after 60
minutes. It applies only to Auto climate control with a heated tube and the
humidifier active. It is never lower than the ambient temperature.

Warm-up restarts after the tube controller has been idle for a minute, for
example between therapy sessions. A hot water chamber, such as after a stock
Warm-up, ends it immediately.

The stock humidifier is open loop. It sets the heater duty from the moisture
the air needs, `duty = k0 + k1 × flow × ΔAH` with flow in L/min and ΔAH in
g/m³, using a calibration the device loads at boot, and only limits the plate
near 63-68 °C. The duty says what the water would evaporate once warm; while
the water is still heating it evaporates less, which is the dry start.

Tube Warm-up therefore estimates the moisture the chamber adds to the room air
as the smaller of two upper bounds:

| Bound | Estimate | Holds |
|-------|----------|-------|
| Plate | the saturation humidity gained from `ambient` to `ambient + coupling × (plate − ambient)`; nothing while the plate is at room temperature | while the water heats |
| Heater power | `(duty − k0) / (k1 × flow)`, with the measured duty `HPM` and average blower flow `AFL` | once the water is warm |

`k0` and `k1` are read at runtime, so each device uses its own calibration.
Stock only runs its model at 8 L/min or more; below that, or without a usable
calibration, only the plate bound applies. The coupling is 0.3 unless set in the menu; that default is the constant in
`patches/tube_warmup_abi.S` and has not been calibrated.

With custom settings, clinical **Accessories** contains:

| Setting | UART | Values | Default |
|---------|------|--------|---------|
| Tube Warm-up | first free on/off slot | Off, On | On |
| Warm-up Factor | RDH | 0.50-1.50, step 0.05 | 1.00 |
| Warm-up Coupling | RDW | 0.10-1.00, step 0.05 | 0.30 |

Each setting needs a label string, and the switch also an on/off variable,
that no other installed feature uses. The `plus` image uses all of them, so
build it without features you don't need. Each one frees what its own settings
used; for example, without ST iBR and AutoEPAP:

```
make build/stm32-plus.bin AIR10_PATCH_ARGS='--patch-st-ibr n --patch-autoepap n --patch-tube-warmup y'
```

Warm-up Factor scales the estimated added moisture: 1.00 uses the estimate as
is, lower values hold the tube back longer.
Warm-up Coupling sets how far the water heats the air leaving the chamber, as a
fraction of the plate's rise above room temperature. Higher values estimate
more moisture while the plate is still warming, so the tube warms sooner; lower
values hold it back longer. It does not change the heater-power bound, so it
only matters during warm-up. Warm-up Factor and the switch are added first, then
Warm-up Coupling. The
[Header climate](#header-climate) switch, when installed, takes what Tube
Warm-up leaves. When no label is left for a setting it is skipped: without
the switch Tube Warm-up stays on, and without the factor or coupling they keep
1.00 and 0.30. The build log reports what was added.

Enable it with `--patch-tube-warmup y` or `PATCH_TUBE_WARMUP=1`. It is off by
default and requires font repacking (on by default).

## Header Clock

Enable **Clock** in clinical **Configuration** to replace the Home and active
therapy-screen titles with the device time in 24-hour `HH:MM` format.

## Header Climate

This optional readout (SX567 firmware) replaces the Home and therapy-screen
titles with the climate values, for example `27.4° 41.0° 85%` (`27° 41° 85%`
with decimals off):

| Field | Source |
|-------|--------|
| Tube temperature | Measured `HTT`; `--` without a heated tube |
| Humidifier plate temperature | Measured `HPT` |
| Relative humidity | Estimated at the tube temperature |

Temperatures follow the device's °C/°F setting. The readout appears only with a
humidifier attached; without one, the clock shows instead. Outside therapy
there is no airflow, so the RH value is the room air at the tube temperature.
Menu screens keep their own titles unless [Always show](#always-show) is on. The
page counter (for example `1/3`) is hidden while the readout is shown.

The device has no humidity sensor in the air path, so the RH value is
estimated in the same way as [Tube Warm-up](#tube-warm-up): the room air plus
the smaller of the plate and heater-power bounds on the added moisture,
divided by the saturation humidity at the tube temperature. With Tube Warm-up
and custom settings, **Warm-up Factor** scales the added moisture. The plate
bound uses the same Warm-up Coupling (default `0.3`, in `patches/header_clock_abi.S`). Treat the value as a guide to the trend, not a
measurement; it has not been checked against a hygrometer.

With custom settings, clinical **Configuration** gets a **Climate** switch
(Off, On; default On) next to **Clock**. Off shows the clock, or the stock
title, again. The switch needs a free on/off variable and label string, like
the Tube Warm-up switch, and gets them after it; without one the readout stays on.

Enable it with `--patch-header-climate y` or `PATCH_HEADER_CLIMATE=1`. It is off
by default, needs the header clock patch (on by default), and is independent
of the **Clock** setting.

### Always show

`--patch-header-climate-always y` shows the readout in every screen title, so
menus such as Settings and My Options show it instead of their names. It needs
`--patch-header-climate`, follows the **Climate** switch, and is a build option
in the patcher window under the header options. Screens without a humidifier
keep their stock titles.

### Decimals

`--patch-header-climate-decimal` (on by default) shows the tube and plate
temperatures with one decimal, `27.4`. Use `--patch-header-climate-decimal n` for
whole degrees, `27`. It is a build option in the patcher window and has no
effect without `--patch-header-climate`.

## Keep Backlight

Hold the dial for three seconds to toggle screen keep-awake.
Home and dial illumination blink once when enabled and twice when disabled.
The switch works during therapy and in standby, and starts Off after reboot.

## Motor Runtime Warning

The patch removes the "Motor life exceeded" nag screen that stock firmware can
show after roughly 20,000 blower runtime hours.

Only the warning screen is removed. The device still tracks and reports its
actual runtime hours,
and no therapy records, service information, or usage data are erased.

## EDF Recording

Stock Air10 variants and therapy modes record different subsets of the
available EDF data. The patch enables a common recording superset so switching
therapy mode does not remove data that the firmware is able to record.

The files remain standard EDF files on the SD card. Signals not used or
produced by the active therapy mode or connected hardware may remain empty or
zero, and the additional channels can make the files slightly larger.

## myAir Cloud Compatibility

myAir accepts sessions only when the selected therapy mode is supported by the
reported device model. Using a mode unlocked from another AirSense or AirCurve
variant can otherwise cause cloud reporting to be rejected.

The patch keeps the reported model compatible with the selected therapy mode
while preserving the device serial number and existing cloud provisioning.

## Replacement LCD Driver

An optional universal LCD driver supports ILI9325 and ILI9328 panels, which are
among the most readily available replacement displays for these devices. This
allows a compatible replacement panel to be used when the original display is
damaged or unavailable.

The controller is detected at startup. Devices with the original ILI9341 panel
continue to use the stock display path, so the same patched image can support
both the original and replacement controllers. Enable this driver when
building with `PATCH_S10_LCD=1`.
