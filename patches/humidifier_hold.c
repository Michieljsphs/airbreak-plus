/* Home held for 2 s or longer, then released, switches the humidifier off, and back on when held again.
 *
 * The Humidity Enable setting (HMX) is the menu's Off/On. The level (HMS) is
 * never touched, so switching on returns to the level used before. While the
 * humidifier is off the blower keeps running, so the hose can dry in use.
 *
 * Auto climate control ignores HMX, so switching off also selects Manual
 * climate control (CCO). Switching on returns to the climate control that was
 * set when the hold switched the humidifier off, Auto or Manual. That is kept
 * in a small block found through the stock worker list (like the climate
 * graph), so it is lost on a reboot: switching on without it returns to Auto.
 */
#include "s10_vars.h"
#include "therapy_worker.h"

extern int variable_get_by_id(int var_id);
extern void variable_set_by_id(int var_id, int raw_value);
extern void *malloc(unsigned size);
extern const unsigned humidifier_hold_max_ms;

enum {
    CLIMATE_AUTO = 0,
    CLIMATE_MANUAL = 1,
    HUMIDIFIER_OFF = 0,
    HUMIDIFIER_ON = 1,
    /* The worker group the node is kept in; it is never enabled. */
    WORKER_GROUP = 2,
    /* No climate control remembered. */
    NONE = 0xff,
};

/* The remembered climate control, in a worker node that is never enabled. */
struct hold_state {
    therapy_worker node;
    unsigned char climate;
};

static void hold_noop(therapy_worker *node) { (void)node; }

static const therapy_worker_method hold_methods[7] = {
    hold_noop, hold_noop, hold_noop, hold_noop, hold_noop, hold_noop, hold_noop
};

static unsigned irq_save(void)
{
    unsigned saved;
    __asm__ volatile ("mrs %0, primask\n\tcpsid i" : "=r" (saved) :: "memory");
    return saved;
}

static void irq_restore(unsigned saved)
{
    __asm__ volatile ("msr primask, %0" :: "r" (saved) : "memory");
}

/* The state block, made on first use; NULL when the heap has no room. */
static struct hold_state *hold_state(void)
{
    struct hold_state *s = (struct hold_state *)therapy_worker_find(WORKER_GROUP, hold_methods);
    if (s)
        return s;
    s = malloc(sizeof(*s));
    if (!s)
        return 0;
    s->climate = NONE;
    /* The stock append links the node enabled, with the base worker methods.
     * The therapy task may be walking the list, so it must not see the node
     * before it has its own methods and is disabled.
     */
    unsigned saved = irq_save();
    therapy_worker_append(&s->node, WORKER_GROUP);
    s->node.methods = hold_methods;
    s->node.enabled = 0;
    irq_restore(saved);
    return s;
}

/* Registered with button_hooks for a Home hold, run on release. A longer hold
 * that another feature acts on at its threshold (humidifier_hold_max_ms, set
 * by the patcher) is left alone, click included.
 */
int humidifier_hold_toggle(unsigned event, unsigned held_ms)
{
    if (held_ms >= humidifier_hold_max_ms)
        return 0;
    struct hold_state *s = hold_state();
    int on = variable_get_by_id(VAR_ID_HMX) != HUMIDIFIER_OFF;
    if (on) {
        if (s)
            s->climate = variable_get_by_id(VAR_ID_CCO) == CLIMATE_MANUAL ?
                         CLIMATE_MANUAL : CLIMATE_AUTO;
        variable_set_by_id(VAR_ID_CCO, CLIMATE_MANUAL);
        variable_set_by_id(VAR_ID_HMX, HUMIDIFIER_OFF);
    } else {
        int climate = s && s->climate != NONE ? s->climate : CLIMATE_AUTO;
        variable_set_by_id(VAR_ID_HMX, HUMIDIFIER_ON);
        variable_set_by_id(VAR_ID_CCO, climate);
        if (s)
            s->climate = NONE;
    }
    return 1;
}
