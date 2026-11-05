# Climate Timer

The climate timer adds a switch and a time to the clinical **Accessories** menu:

| Setting | What it does |
|---------|--------------|
| **Hum. off timer** | Off (default) or On. The time below only shows while it is On. |
| **Hum. off at (h)** | During therapy, switches the humidifier off (**Humidity** Off) at this time. The heated tube keeps running with dry air and is dry by the morning. When therapy stops, the humidifier is switched back on. |

The time is in hours with quarter-hour steps: `6.75` is 06:45, `0.00` is
midnight. It starts at `6.00` (06:00). Switching the timer Off keeps the time.

The stock firmware dries the tube after therapy by blowing air through the
humidifier. If you take the humidifier out to let it dry, that air no longer
reaches the tube. **Hum. off at** dries the tube during the last part of the
night instead.

## Details

- **Humidifier off** acts once per therapy session, when the set time is
  reached, or when therapy starts up to 4 hours after it. If you switch the
  humidity back on, the timer leaves it on.
- The tank still holds warm water when the plate switches off, so set the time
  about 30 to 60 minutes before you usually get up.
- The timer runs with the screen off. It uses the device clock, so check the
  time under Configuration.
- Humidity is a saved setting. The timer remembers that it switched it off only
  in RAM: after a power cut during that part of the night, switch **Humidity**
  back on by hand.

## Enabling

```bash
python python/patch-airsense.py INPUT OUTPUT PATCH --patch-climate-timer y
```

Or `PATCH_CLIMATE_TIMER=1`. It needs SX567 firmware, the header clock
(on by default) and the custom settings. Each timer needs two free label strings, an
on/off setting and a numeric setting; when they run out, the patcher reports
which timer it left out. Build without another custom setting (for example
`--patch-st-ibr n`, `--patch-fall-time n` or `--patch-autoepap n`) to make room.
