#ifndef HUMIDITY_MODEL_H
#define HUMIDITY_MODEL_H

/* The stock humidifier model, run backwards.
 *
 * Stock sets the plate from the moisture the air needs: duty = k0 + k1 * flow
 * * dAH, plate = k3 + k4 * duty, with flow in L/min and dAH in g/m3. Solving
 * for dAH gives the moisture the chamber adds at the current plate temperature,
 * using the device's own calibration; below k3 the same line is extended down
 * to where it reaches zero. Stock only runs the model at 8 L/min or
 * more; below that, and without usable coefficients, the outlet air is taken
 * as saturated at a fixed share of the plate's rise above ambient.
 */

enum {
    /* Climate signal the stock controller reads as flow, L/s. */
    HUMIDITY_SIGNAL_FLOW = 0xa2,
};

#define HUMIDITY_MIN_FLOW 8.0f

struct humidity_coeffs {
    float k0, k1, k3, k4;
};

extern float climate_signal_get(int id);
extern void humidity_model_coefficients_get(void *unused, float *k0, float *k1,
                                            float *k2, float *k3, float *k4);

/* Stock saturation absolute humidity, g/m3. */
static inline float ah_sat(float t)
{
    return 4.61f + t * (0.436f + t * (0.00122f + t * 0.000427f));
}

/* Leaves k1 at zero when the coefficients can't be inverted. */
static inline void humidity_coeffs_read(struct humidity_coeffs *c)
{
    float k2;
    /* The getter writes nothing when the calibration is missing. */
    c->k1 = 0.0f;
    humidity_model_coefficients_get(0, &c->k0, &c->k1, &k2, &c->k3, &c->k4);
    if (!(c->k4 > 0.0f && c->k3 >= 20.0f && c->k3 <= 63.0f))
        c->k1 = 0.0f;
}

/* Absolute humidity leaving the chamber, g/m3: the room air plus factor times
 * the moisture the model adds, or the coupling estimate without usable
 * coefficients or flow. Never drier than the room.
 */
static inline float humidity_outlet(const struct humidity_coeffs *c, float plate,
                                    float ambient, float room_rh, float factor,
                                    float coupling)
{
    float room = room_rh * ah_sat(ambient);
    float flow = c->k1 > 0.0f ? climate_signal_get(HUMIDITY_SIGNAL_FLOW) * 60.0f : 0.0f;
    if (!(flow >= HUMIDITY_MIN_FLOW)) {
        float sat = ah_sat(ambient + coupling * (plate - ambient));
        return sat > room ? sat : room;
    }
    float duty = (plate - c->k3) / c->k4;
    float added = ((duty < 1.0f ? duty : 1.0f) - c->k0) / (c->k1 * flow);
    return room + factor * (added > 0.0f ? added : 0.0f);
}

/* Temperature in [lo, hi] where rh * ah_sat(T) equals ah. */
static inline float humidity_model_temperature(float ah, float rh, float lo, float hi)
{
    if (rh * ah_sat(lo) >= ah)
        return lo;
    if (rh * ah_sat(hi) <= ah)
        return hi;
    for (int i = 0; i < 16; ++i) {
        float mid = 0.5f * (lo + hi);
        if (rh * ah_sat(mid) < ah)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5f * (lo + hi);
}

#endif
