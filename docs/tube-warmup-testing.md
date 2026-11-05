# Tube Warm-up: field manual

Notes for testing the Tube Warm-up and header climate readout on the device.
The estimate has not been checked against a hygrometer, so treat every number
as a guide until you have measured it yourself.

## What it does

At the start of therapy the stock device heats the tube straight away, while the
water chamber needs many minutes. The early air is warm and dry. Tube Warm-up
caps the tube setpoint at the temperature where the estimated humidity gives
your target RH, and lets that cap rise as the plate warms. The cap is never
below 16 °C, the lowest tube setting, and the tube heater stays off while the
measured tube is above the cap.

Warm-up ends when the first of these happens:

- the cap reaches your tube setting;
- the plate has stopped rising: it is checked every 4 minutes and must have
  risen less than 0.2 °C per minute on average, while at least 8 °C above the
  ambient reading;
- 60 minutes have passed.

The tube is then released to its setting. The humidity model keeps aiming for
that setting until the measured tube is within 1 °C of it, for at most 5
minutes, so the plate does not cool at the hand-off. After that the stock
humidifier control runs unchanged.

### How fast the tube heats

How fast the tube warms depends on how far the setpoint is above the measured
temperature:

- Released to its full setting, with a gap of 3 °C or more, it heated at about
  2 °C per minute in the logs.
- Held just below a cap that is only 1 to 3 °C above it, it heated at about
  0.3 to 0.6 °C per minute.

So the cap not only limits the temperature, it also sets how hard the heater
works. A higher cap, from a lower target or a higher coupling, makes the tube
warm both higher and faster.

## What you see

The header shows `tube° plate° RH%`, for example `24.9° 27.5° 45%`.

| Field | Source |
|-------|--------|
| Tube | Measured `HTT` |
| Plate | Measured `HPT` |
| RH | Estimated at the tube temperature. It cannot exceed 100 %. |

`100 %` means the estimate is at or above saturation at the tube temperature,
not exactly 100 %. The estimate is an upper bound on what the chamber adds, so
the real air is usually drier.

## The settings

Clinical menu, Accessories:

| Setting | What it changes | Default |
|---------|-----------------|---------|
| Target RH (%) | The humidity the stock controller aims for, and where the cap sits | 85 |
| Tube Warm-up | Switch for the whole feature | On |
| Warm-up Coupling | How warm the chamber air is assumed to be, during warm-up only | 0.30 |

The tube temperature setting is the stock one. Climate (in Configuration) turns
the header readout on and off.

### When to change the coupling

The coupling is the plate bound: the fraction of the plate's rise above room
temperature over which the air picks up moisture. It only matters while the
plate is warming and that bound is the smaller one. It has no effect once the
heater-power bound takes over, which is usually when the plate is hot.

| Symptom at the start | Change |
|----------------------|--------|
| Tube warms while the air is still dry | Lower the coupling (0.30 to 0.20) |
| Tube is held cold for a long time, or you see condensation early on | Raise the coupling (0.30 to 0.40) |
| Header shows far above target for several minutes and the tube is already heating hard | Nothing. The tube is catching up, not held by a setting |

Change it in steps of 0.05 to 0.10 and compare the first 10 minutes of a
session. A change only shows in those first minutes.

### Target RH and the tube setting

- A high target (95 to 98 %) keeps the air within about 1 °C of its dew point.
  Condensation in the tube is likely. 85 to 90 % gives more margin.
- Lowering the target raises the cap, so the tube is allowed to warm earlier and
  faster.
- Lowering the tube setting lowers the final tube temperature and the
  humidity needed to reach the target. If the humidifier cannot reach the target
  at your tube setting, the header settles below it. A lower tube setting, a
  lower target, or a lower flow brings them together.

### If the header disagrees with reality

Nothing in the menu scales the estimate in steady state, because the coupling
only affects warm-up. A hygrometer at the mask, or weighing the water tub before
and after a night, tells you how far off it is. The weighed amount can be
compared with the estimated one:

```
water per hour (g) = added moisture (g/m3) x flow (L/min) x 0.06
```

For example 13.7 g/m3 at 39.5 L/min is about 32 g per hour. If the real figure
is very different, tell me and the estimate can be changed in the source.

## How to run a test

1. Fill the water tub to its normal line. A nearly empty tub heats the plate
   too fast and also makes the output less predictable. Do not run for long with
   little water.
2. Use a realistic flow. The added moisture per heater power falls as flow
   rises, so a high-flow test understates what the humidifier can do. Run with a
   mask and your therapy pressure if you can.
3. Start the session with the tube at room temperature.
4. In a terminal, from the repo folder in WSL with the ST-LINK attached:

   ```bash
   ./read-climate.sh -w 10 -c 0.30 -r 20 | tee climate-log.txt
   ```

   `-c` must match Warm-up Coupling in the device menu. `-r` is the real room
   temperature. Besides the variables, each reading then prints the estimated
   absolute humidity at the mask end, the RH at the tube (not limited to 100 %
   here) and the dew point. Add `-k` if your calibration `k1` is not 0.001.

5. Note the time, `HPT`, `HTT`, `HPM`, and the header values every few minutes
   for the first 20 minutes, then once more after 30 to 60 minutes.
6. Check the tube near the mask end for droplets.

What a good run looks like:

- `HTT` stays close to room temperature at first, then rises as `HPT` rises.
- The header RH falls from 100 % toward your target as the tube warms.
- By the time `HPT` stops rising, `HTT` is near its setting and the header is
  near the target.
- Little or no condensation.
- The plate dips by 1 to 2 °C at most when the tube is released.

## Reading the variables

| Var | Meaning | Real value |
|-----|---------|-----------|
| `AFL` | Average blower flow | raw / 100 = L/min |
| `HPT` | Plate temperature | raw / 10 = °C |
| `HTT` | Tube temperature, measured | raw / 10 = °C |
| `HTS` | Tube temperature setting | raw / 10 = °C |
| `HPM` | Heater duty | raw = % |
| `HAT` | Ambient temperature | raw / 10 = °C |
| `HAH` | Ambient RH | raw / 10 = % |

`HAT` reads high, often 5 to 10 °C above the room, because the sensor is in the
warm case. The room's moisture is still right, because the sensor's temperature
and RH are consistent with each other.

The plate stays at exactly 68 °C once it reaches its limit, and `HPM` stays
nearly constant (about 52 to 54 %) whatever the flow. Do not read `HPM` as the
demanded duty.

## Troubleshooting

| Observation | Likely reason |
|-------------|---------------|
| Header shows 100 % for the first minutes | Normal. The tube is cooler than the dew point of the estimated air, and the estimate is an upper bound |
| Tube climbs fast at the start | The cap is well above the tube. The plate bound is large, or the tube was already warm. Lower the coupling |
| Tube is at room temperature for a long time | The cap is low because the plate is cold, or the target is high. Raise the coupling or lower the target |
| Header settles well below the target at the end | The humidifier cannot supply it at this flow and tube setting. Lower the tube setting, the target, or the flow |
| Condensation | Lower the target, or raise the coupling a little so the tube warms earlier |
| Nothing changes when you toggle the switch | Warm-up only works in Auto climate control with a heated tube and the humidifier on |
| A setting is missing from the menu | The build ran out of label strings. See `docs/guide/features/general.md` |

## Warm-up can restart

Warm-up restarts after the tube controller has been idle for more than a minute,
for example after a pause with the mask off. If the plate is still hot, it ends
immediately. If the plate has cooled, the tube can be held back again briefly.

## Limits

- The coupling (0.30) is not calibrated.
- The estimate was tuned on one device and one calibration (`k0 = 0`,
  `k1 = 0.001`).
- This is a hobbyist modification. Do not rely on it for therapy decisions.
