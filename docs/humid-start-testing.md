# Humid Start: field manual

Notes for testing the Humid Start and header climate readout on the device.
The estimate has not been checked against a hygrometer, so treat every number
as a guide until you have measured it yourself.

## What it does

At the start of therapy the stock device heats the tube straight away, while the
water chamber needs many minutes. The early air is warm and dry. Humid Start
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
  2 °C per minute in tests.
- Held just below a cap that is only 1 to 3 °C above it, it heated at about
  0.3 to 0.6 °C per minute.

So the cap not only limits the temperature, it also sets how hard the heater
works. A higher cap, from a lower target or a higher coupling, makes the tube
warm both higher and faster.

## How the moisture is estimated

The device has no humidity sensor in the air path, so the moisture is worked out
from other readings:

```
ah_sat(T) = 4.61 + 0.436 T + 0.00122 T^2 + 0.000427 T^3          (g/m3)
room      = HAH x ah_sat(HAT)                                    moisture of the room air
plate     = ah_sat(HAT + coupling x (HPT - HAT)) - ah_sat(HAT)   bound from the plate temperature
power     = (HPM/100 - k0) / (k1 x AFL)                          bound from the heater power
added     = the smaller of plate and power, never below 0
outlet    = room + added
RH shown  = 100 x outlet / ah_sat(HTT), limited to 0 to 100
```

`power` is only used above 8 L/min and with a valid calibration.

### Why the power bound cannot be used during warm-up

The power bound assumes all the heater power turns into vapour. That is only
true once the water is warm. While it warms, most of the power heats the water
and the plate, and only what is left evaporates. The heater duty also carries no
warm-up information: it stays at 51 to 54 % from the first sample, even before
any flow. So the power bound claims about 20 g/m3 of added moisture in the first
minute, when the water is cold. A cap built on that would let the tube heat
straight away, which is the stock problem again.

In steady state the heat in equals the evaporation plus the losses, so the power
bound is the right figure there. The plate bound has the right shape for
warm-up, because the moisture rises as the water warms, but its scale (the
coupling) is a guess.

### Where the bounds cross

With a coupling of 0.30 the power bound became the smaller one after about 2 to
3 minutes in test runs. After that the estimate was flat at about 13 g/m3,
while the plate was still climbing from 56 to 68 °C. Real moisture almost
certainly kept rising over those minutes, so 0.30 is probably too generous.

### Calibrating the coupling from steady state

Once the plate sits at its limit, the power bound is the stock model's own
prediction of the moisture. Choose the coupling so the plate bound reaches that
same value at that point:

```
ah_sat(T) = ah_sat(HAT) + power          solve for T
coupling  = (T - HAT) / (HPT - HAT)
```

Example: plate 68.0 °C, ambient 32 °C, power bound 13.7 g/m3 gives T = 38.5 °C and
a coupling of about **0.18**. A second test run gave about 0.175. This assumes the stock
calibration is right in steady state. With a coupling near 0.18 the plate bound
stays the active limit through most of warm-up, rises smoothly from about 8 to
13.7 g/m3, and meets the power bound at the plateau.

## What you see

The header shows `tube° plate° RH%`, for example `24.9° 27.5° 45%`.

| Field | Source |
|-------|--------|
| Tube | Measured `HTT` |
| Plate | Measured `HPT` |
| RH | Estimated at the tube temperature. It cannot exceed 100 %. |

`100 %` means the estimate is at or above saturation at the tube temperature,
not exactly 100 %. Air cannot hold more than saturation, so when the estimate is
above 100 % the real air holds less moisture than the estimate says.

The estimate is not a proven upper bound. It is probably too high in the first
minutes, because the water lags the plate, and when it is clamped at 100 %. In
steady state it is the stock model's own prediction, which could be off in
either direction. A hygrometer or the water-weighing check below is the only way
to find out.

## The settings

Clinical menu, Accessories:

| Setting | What it changes | Default |
|---------|-----------------|---------|
| Target RH (%) | The humidity the stock controller aims for, and where the cap sits | 85 |
| Humid Start | Switch for the whole feature | On |
| Min Tube (C) | The lowest the cap goes, so the first air is at least this warm | 16 |
| Start Coupling | How warm the chamber air is assumed to be, during warm-up only | 0.30 |

In a cold room, raise Min Tube (for example to 23). The tube then heats to it
right away and waits there until the RH catches up. The higher it is, the warmer
and drier the first minutes are.

The tube temperature setting is the stock one. Climate (in Configuration) turns
the header readout on and off.

### When to change the coupling

The coupling is the plate bound: the fraction of the plate's rise above room
temperature over which the air picks up moisture. It only matters while the
plate is warming and that bound is the smaller one. It has no effect once the
heater-power bound takes over.

**A lower coupling gives a lower moisture estimate, so a lower cap, so the tube
is held colder for longer while the real air gets humid. That means more
condensation risk.** A higher coupling does the opposite: the tube warms sooner
and the early air is drier.

| Symptom at the start | Change |
|----------------------|--------|
| Tube warms while the air is still dry | Lower the coupling (0.30 to 0.25) |
| Tube is held cold for a long time, or you see condensation early on | Raise the coupling (0.30 to 0.40) |
| Header shows far above target for several minutes and the tube is already heating hard | Nothing. The tube is catching up, not held by a setting |

Change it in steps of 0.05 and compare the first 10 minutes of a session. A
change only shows in those first minutes.

### Coupling test plan for sleep nights

Aim: the lowest coupling that does not condense, which holds the tube back the
most without rain-out.

1. Night 1: 0.30, the baseline.
2. Night 2: 0.25, if night 1 was clean.
3. Night 3: 0.20, if night 2 was clean.
4. Stop at the first sign of condensation and keep the previous value. If 0.30
   already condenses, go up to 0.35 to 0.40.

Keep everything else the same on every night: tube setting, target RH (90 %
reduces confounding from steady-state condensation), full water tub, same
pressure.

The effect is small and only in the first minutes. In a test run the plate bound
was the smaller one for about 3 minutes at 0.30, 4 to 5 minutes at 0.25 and about
7 minutes at 0.20. So the tube temperatures at 0.30 and 0.25 may differ by less
than 1 °C, and you may not see a difference by eye. `HTT` over the first 15
minutes is a more sensitive measure than condensation.

Checking for condensation:

- Look at the mask end of the tube and inside the mask as soon as you wake up,
  and note any gurgling or water in the mask.
- Condensation from the first 20 minutes may have re-evaporated by morning.
- One night per value is weak evidence. Two nights per value is better.

Log each night with `-c` matching the menu value:

```bash
./read-climate.sh -w 20 -c 0.25 -r 20 | tee night-c025.txt
```

## Flow and the achievable humidity

The added moisture per heater power is inversely proportional to flow. At the
plate limit (68 °C, about 54 % duty) the device adds about 540 / flow g/m3. The
average flow depends on the pressure, the mask's vent and any leak, not on
whether you are awake or asleep. Your own breathing adds almost nothing to the
average.

At an average pressure of about 11 cmH2O the average flow is typically in the
region of 30 to 40 L/min, depending on the mask and any leak. These are
estimates. Using a room moisture of 10.9 g/m3:

| Average flow | RH at a 30 °C tube | Tube setting for 95 % |
|--------------|--------------------|-----------------------|
| 40 L/min | about 81 % | about 27 °C |
| 35 L/min | about 87 % | about 28.3 °C |
| 32 L/min | about 92 % | about 29.4 °C |
| 30 L/min | about 95 % | about 30 °C |

At these flows, expect 85 to 90 % at a 30 °C tube. For 95 % set the tube to about
27 to 28 °C, or accept 85 to 90 %.

The plate stays at exactly 68 °C once it reaches its limit. Raising that limit
changes a thermal safety limit of the humidifier and is not recommended. ResMed's
published humidifier output in the user guide is above 10 mg/L at Setting 8.

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
is very different, the estimate can be changed in the source.

## How to run a test

1. Fill the water tub to its normal line. A nearly empty tub heats the plate
   too fast and also makes the output less predictable. Do not run for long with
   little water.
2. Use a realistic flow: a mask and your therapy pressure.
3. Start the session with the tube at room temperature.
4. In a terminal, from the repo folder in WSL with the ST-LINK attached:

   ```bash
   ./read-climate.sh -w 10 -c 0.30 -r 20 | tee climate-log.txt
   ```

   `-c` must match Start Coupling in the device menu. `-r` is the real room
   temperature. Besides the variables, each reading then prints the estimated
   absolute humidity at the mask end, the RH at the tube (not limited to 100 %
   here) and the dew point. Add `-k` if your calibration `k1` is not 0.001. Use
   `-w 20` for an overnight log.

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

`HPM` stays nearly constant (about 52 to 54 %) whatever the flow. Do not read it
as the demanded duty.

## Troubleshooting

| Observation | Likely reason |
|-------------|---------------|
| Header shows 100 % for the first minutes | Normal. The tube is cooler than the dew point of the estimated air, and the estimate is probably high early on |
| Tube climbs fast at the start | The cap is well above the tube. The plate bound is large, or the tube was already warm. Lower the coupling |
| Tube is at room temperature for a long time | The cap is low because the plate is cold, or the target is high. Raise the coupling or lower the target |
| Header settles well below the target at the end | The humidifier cannot supply it at this flow and tube setting. Lower the tube setting, the target, or the flow |
| Estimated moisture is flat after the first 3 minutes | The power bound took over early. The coupling is probably too high; try 0.20 to 0.25 |
| Condensation | Lower the target, or raise the coupling a little so the tube warms earlier |
| Nothing changes when you toggle the switch | Warm-up only works in Auto climate control with a heated tube and the humidifier on |
| A setting is missing from the menu | The build ran out of label strings. See `docs/guide/features/humid_start.md` |

## Warm-up can restart

Warm-up restarts after the tube controller has been idle for more than a minute,
for example after a pause with the mask off. If the plate is still hot, it ends
immediately. If the plate has cooled, the tube can be held back again briefly.

## Limits

- The coupling (0.30) is not calibrated. About 0.18 is the steady-state estimate.
- The estimate was tuned on one device and one calibration (`k0 = 0`,
  `k1 = 0.001`).
- The estimate is not a proven upper bound; it can be off in either direction.
- This is a hobbyist modification. Do not rely on it for therapy decisions.
