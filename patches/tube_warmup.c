/* Limit heated-tube warm-up to the humidity the water chamber can supply.
 *
 * The stock climate controller drives the tube toward HTS as soon as therapy
 * starts, long before the plate has heated the water, so early air is warm and
 * dry. While the plate warms, cap the tube setpoint where the estimated vapour
 * pressure gives the target RH, and let the humidity model aim for HTS so the
 * plate keeps heating at full demand. The cap ends once it reaches HTS, the
 * plate stops rising, or the warm-up time limit expires.
 */
typedef unsigned char u8;
typedef float (*getf_t)(void *);
typedef int (*geti_t)(void *);

struct obj {
    void *const *vptr;
};

enum {
    INPUT_SLOTS = 18,
    INPUT_TUBE_PRESENT = 0x1c / 4,
    INPUT_HUMIDIFIER_ACTIVE = 0x20 / 4,
    INPUT_CLIMATE_MODE = 0x34 / 4,
    INPUT_TARGET_RH = 0x44 / 4,
    SENSOR_PLATE = 0x0c / 4,
    SENSOR_AMBIENT_T = 0x10 / 4,
    SENSOR_AMBIENT_RH = 0x14 / 4,
    /* The climate controller stores its sensors at +0x14 and inputs at +0x28. */
    SENSORS_TO_INPUTS = 0x14,
    CLIMATE_MODE_AUTO = 1,
    SAMPLE_MS = 60000,
    IDLE_MS = 60000,
    MAX_MINUTES = 60,
    STATE_MAGIC = 0x50555754,
};

/* Cloning the inputs vtable lets both hooks find this state from their own
 * object without reserving a writable global.
 */
struct state {
    void *methods[INPUT_SLOTS];
    unsigned magic;
    unsigned seen[3];
    unsigned sample[3];
    float last_plate;
    u8 minutes;
    u8 done;
    u8 capping;
};

extern void *malloc(unsigned size);
extern float expf(float x);
extern void millisecond_timer_init(void *timer, unsigned elapsed);
extern int millisecond_timer_expired(void *timer, unsigned ms);
extern float climate_tube_setpoint_get(struct obj *inputs);
extern float climate_reference_temperature_get(struct obj *sensors);
extern void *const climate_inputs_vtable[];
extern const float tube_warmup_plate_coupling;

static float call_f(struct obj *o, int slot) { return ((getf_t)o->vptr[slot])(o); }
static int call_i(struct obj *o, int slot) { return ((geti_t)o->vptr[slot])(o); }

static float log_approx(float x)
{
    union { float f; int i; } u = { .f = x };
    int e = ((u.i >> 23) & 0xff) - 127;
    u.i = (u.i & 0x007fffff) | 0x3f800000;
    if (u.f > 1.41421356f) {
        u.f *= 0.5f;
        ++e;
    }
    float s = (u.f - 1.0f) / (u.f + 1.0f), s2 = s * s;
    return (float)e * 0.69314718f +
           2.0f * s * (1.0f + s2 * (0.33333333f + s2 * (0.2f + s2 * 0.14285714f)));
}

/* Magnus saturation vapour pressure over water, hPa. */
static float saturation(float t)
{
    return 6.112f * expf(17.62f * t / (243.12f + t));
}

static float dew_point(float vapour)
{
    float x = log_approx(vapour / 6.112f);
    return 243.12f * x / (17.62f - x);
}

static void restart(struct state *s, float plate)
{
    s->done = 0;
    s->minutes = 0;
    s->last_plate = plate;
    millisecond_timer_init(s->sample, 0);
}

static struct state *state_of(struct obj *inputs, int create)
{
    struct state *s = (struct state *)inputs->vptr;
    if (inputs->vptr != climate_inputs_vtable)
        return s->magic == STATE_MAGIC ? s : 0;
    if (!create || !(s = malloc(sizeof(*s))))
        return 0;
    for (unsigned i = 0; i < INPUT_SLOTS; ++i)
        s->methods[i] = climate_inputs_vtable[i];
    s->magic = STATE_MAGIC;
    s->capping = 0;
    millisecond_timer_init(s->seen, 0);
    /* The first plate sample must not read as settled. */
    restart(s, -1000.0f);
    inputs->vptr = (void *const *)s->methods;
    return s;
}

/* Matches the stock reference-temperature branch that returns the measured
 * tube temperature: a heated tube in Auto climate control.
 */
static int auto_with_tube(struct obj *inputs)
{
    return call_i(inputs, INPUT_TUBE_PRESENT) &&
           call_i(inputs, INPUT_CLIMATE_MODE) == CLIMATE_MODE_AUTO;
}

/* Replaces the climate inputs' tube-setpoint getter (HTS, degC). */
float start(struct obj *inputs)
{
    float set = climate_tube_setpoint_get(inputs);
    struct state *s = state_of(inputs, 1);
    if (!s)
        return set;
    struct obj *sensors = (struct obj *)((u8 *)inputs - SENSORS_TO_INPUTS);
    float plate = call_f(sensors, SENSOR_PLATE);
    /* The tube controller only asks during therapy; a long gap starts a session. */
    if (millisecond_timer_expired(s->seen, IDLE_MS))
        restart(s, plate);
    millisecond_timer_init(s->seen, 0);
    s->capping = 0;
    if (s->done || !call_i(inputs, INPUT_HUMIDIFIER_ACTIVE) || !auto_with_tube(inputs))
        return set;

    float ambient = call_f(sensors, SENSOR_AMBIENT_T);
    if (millisecond_timer_expired(s->sample, SAMPLE_MS)) {
        millisecond_timer_init(s->sample, 0);
        /* A plate that stopped rising has reached the model's target. */
        if (++s->minutes >= MAX_MINUTES ||
                (plate - s->last_plate < 0.2f && plate > ambient + 5.0f)) {
            s->done = 1;
            return set;
        }
        s->last_plate = plate;
    }

    float outlet = ambient + tube_warmup_plate_coupling * (plate - ambient);
    float vapour = saturation(outlet);
    float room = call_f(sensors, SENSOR_AMBIENT_RH) * saturation(ambient);
    if (vapour < room)
        vapour = room;
    float limit = dew_point(vapour / call_f(inputs, INPUT_TARGET_RH));
    if (limit >= set) {
        s->done = 1;
        return set;
    }
    s->capping = 1;
    return limit > ambient ? limit : ambient;
}

/* Replaces the sensors' humidity-model reference temperature. */
float tube_warmup_reference(struct obj *sensors)
{
    struct obj *inputs = (struct obj *)((u8 *)sensors + SENSORS_TO_INPUTS);
    struct state *s = state_of(inputs, 0);
    if (s && s->capping && auto_with_tube(inputs))
        return climate_tube_setpoint_get(inputs);
    return climate_reference_temperature_get(sensors);
}
