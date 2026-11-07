/* Home held for 2 s switches the humidifier off, and back on when held again.
 *
 * The Humidity Enable setting (HMX) is the menu's Off/On. The level (HMS) is
 * never touched, so switching on returns to the level used before. While the
 * humidifier is off the blower keeps running, so the hose can dry in use.
 * No state is kept: the on value is the one the menu writes for On.
 */
#include "s10_vars.h"

extern int variable_get_by_id(int var_id);
extern void variable_set_by_id(int var_id, int raw_value);

enum {
    HUMIDIFIER_OFF = 0,
    HUMIDIFIER_ON = 1,
};

/* Registered with button_hooks for a Home hold. Returns nonzero when handled. */
int humidifier_hold_toggle(unsigned event, unsigned held_ms)
{
    int on = variable_get_by_id(VAR_ID_HMX) != HUMIDIFIER_OFF;
    variable_set_by_id(VAR_ID_HMX, on ? HUMIDIFIER_OFF : HUMIDIFIER_ON);
    return 1;
}
