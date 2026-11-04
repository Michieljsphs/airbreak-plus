/* Limit heated-tube warm-up to the humidity the water chamber can supply.
 *
 * The stock climate controller drives the tube toward HTS as soon as therapy
 * starts, long before the plate has heated the water, so early air is warm and
 * dry. While the plate warms, cap the tube setpoint where the estimated moisture
 * the chamber adds (humidity_model.h) gives the target RH, and let the stock
 * model aim for HTS so the plate keeps heating at full demand. The cap is never
 * below the lowest tube setting (16 degC). The cap ends once
 * it reaches HTS, the plate stops rising, or the warm-up time limit expires.
 * The stock model keeps aiming for HTS until the measured tube is within a
 * degree of it, so the plate does not cool when the tube is released.
 */
#include "s10_vars.h"
#include "humidity_model.h"

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
    /* Minutes between plate checks; a power of two. */
    WINDOW_MINUTES = 4,
    HANDOFF_MS = 300000,
    STATE_MAGIC = 0x50555754,
};

/* The lowest tube temperature setting, degC. */
#define MIN_TUBE_TEMP 16.0f
/* How close the measured tube must get to HTS before the stock reference
 * temperature takes over again, degC.
 */
#define HANDOFF_MARGIN 1.0f

/* Cloning the inputs vtable lets both hooks find this state from their own
 * object without reserving a writable global.
 */
struct state {
    void *methods[INPUT_SLOTS];
    unsigned magic;
    unsigned seen[3];
    unsigned sample[3];
    float last_plate;
    struct humidity_coeffs coeffs;
    u8 minutes;
    u8 done;
    u8 capping;
};

extern void *malloc(unsigned size);
extern void millisecond_timer_init(void *timer, unsigned elapsed);
extern int millisecond_timer_expired(void *timer, unsigned ms);
extern float climate_tube_setpoint_get(struct obj *inputs);
extern float climate_reference_temperature_get(struct obj *sensors);
extern void *const climate_inputs_vtable[];
extern const float tube_warmup_plate_coupling;
extern const unsigned short tube_warmup_enable_var_id;
extern const unsigned short tube_warmup_factor_var_id;
extern const unsigned short tube_warmup_coupling_var_id;
extern int variable_get_by_id(int var_id);

static float call_f(struct obj *o, int slot) { return ((getf_t)o->vptr[slot])(o); }
static int call_i(struct obj *o, int slot) { return ((geti_t)o->vptr[slot])(o); }

/* Custom settings, when installed, supply the switch and the factor. */
static int enabled(void)
{
    return tube_warmup_enable_var_id == 0xffffu ||
           variable_get_by_id(tube_warmup_enable_var_id);
}

/* Scales the estimated moisture the chamber adds. */
static float factor(void)
{
    if (tube_warmup_factor_var_id == 0xffffu)
        return 1.0f;
    return variable_get_by_id(tube_warmup_factor_var_id) * 0.01f;
}

/* Plate bound coupling: the custom setting, else the built-in constant. */
static float coupling(void)
{
    if (tube_warmup_coupling_var_id == 0xffffu)
        return tube_warmup_plate_coupling;
    return variable_get_by_id(tube_warmup_coupling_var_id) * 0.01f;
}

static void restart(struct state *s, float plate)
{
    s->done = 0;
    s->minutes = 0;
    s->last_plate = plate;
    humidity_coeffs_read(&s->coeffs);
    millisecond_timer_init(s->sample, 0);
}

/* Ends warm-up. The sample timer is free from here on and times the hand-off. */
static void finish(struct state *s)
{
    s->done = 1;
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
    /* After warm-up the reference stays at HTS until the tube has caught up. */
    s->capping = s->done &&
                 variable_get_by_id(VAR_ID_HTT) < set * 10.0f - 10.0f * HANDOFF_MARGIN &&
                 !millisecond_timer_expired(s->sample, HANDOFF_MS);
    if (s->done || !enabled() || !call_i(inputs, INPUT_HUMIDIFIER_ACTIVE) ||
            !auto_with_tube(inputs))
        return set;

    float ambient = call_f(sensors, SENSOR_AMBIENT_T);
    if (millisecond_timer_expired(s->sample, SAMPLE_MS)) {
        millisecond_timer_init(s->sample, 0);
        /* A plate that barely rose over the last window has reached the model's
         * target. The plate wobbles and can stall for a minute or two while
         * still heating, so compare over several minutes, not one.
         */
        int check = !(++s->minutes & (WINDOW_MINUTES - 1));
        if (s->minutes >= MAX_MINUTES ||
                (check && plate - s->last_plate < 0.2f * WINDOW_MINUTES &&
                 plate > ambient + 8.0f)) {
            finish(s);
            return set;
        }
        if (check)
            s->last_plate = plate;
    }

    float outlet = humidity_outlet(&s->coeffs, plate, ambient,
                                   call_f(sensors, SENSOR_AMBIENT_RH),
                                   variable_get_by_id(VAR_ID_HPM) * 0.01f,
                                   variable_get_by_id(VAR_ID_AFL) * 0.01f, factor(),
                                   coupling());
    /* The floor is the lowest tube setting, not the ambient reading: the sensor
     * sits in the warm case, and a floor above the tube would keep it heating.
     */
    float limit = humidity_model_temperature(outlet, call_f(inputs, INPUT_TARGET_RH),
                                             set < MIN_TUBE_TEMP ? set : MIN_TUBE_TEMP, set);
    if (limit + 0.01f >= set) {
        finish(s);
        return set;
    }
    s->capping = 1;
    return limit;
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
