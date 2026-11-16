/* Home held for 2 s or longer, then released, switches the humidifier off, and back on when held again.
 *
 * The Humidity Enable setting (HMX) is the menu's Off/On. The level (HMS) is
 * never touched, so switching on returns to the level used before. While the
 * humidifier is off the blower keeps running, so the hose can dry in use.
 * No state is kept: switching on always returns to Auto climate control.
 */
#include "s10_vars.h"

extern int variable_get_by_id(int var_id);
extern void variable_set_by_id(int var_id, int raw_value);
extern const unsigned humidifier_hold_max_ms;

enum {
    CLIMATE_AUTO = 0,
    CLIMATE_MANUAL = 1,
    HUMIDIFIER_OFF = 0,
    HUMIDIFIER_ON = 1,
};

/* Registered with button_hooks for a Home hold, run on release. A longer hold
 * that another feature acts on at its threshold (humidifier_hold_max_ms, set
 * by the patcher) is left alone, click included.
 */
int humidifier_hold_toggle(unsigned event, unsigned held_ms)
{
    if (held_ms >= humidifier_hold_max_ms)
        return 0;
    int on = variable_get_by_id(VAR_ID_HMX) != HUMIDIFIER_OFF;
    if (on) {
        /* Auto climate control ignores HMX, so leave Auto while it is off. */
        variable_set_by_id(VAR_ID_CCO, CLIMATE_MANUAL);
        variable_set_by_id(VAR_ID_HMX, HUMIDIFIER_OFF);
    } else {
        /* Off is Manual with HMX off; switching on returns to Auto. */
        variable_set_by_id(VAR_ID_HMX, HUMIDIFIER_ON);
        variable_set_by_id(VAR_ID_CCO, CLIMATE_AUTO);
    }
    return 1;
}
