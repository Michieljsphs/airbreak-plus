/*
 * sd_warning.c - Show the stock "No SD card detected" message when therapy
 * starts without an SD card, not only at power-up.
 *
 * Stock firmware runs a periodic card check (sd_warning_check_hook wraps one
 * call inside it). On the first check after power-up, a missing card with
 * SCS=1 sets ZPK bit 0, which the GUI shows as SPM=1 "No SD card detected,
 * press OK to continue". Every later check clears that bit again unless the
 * machine is in Standby with no card, so stock never shows it in therapy.
 *
 * This payload:
 *  - passes "keep" to the stock check during therapy too, so the bit is only
 *    cleared once a card is inserted;
 *  - runs after every ZRM change (variable callback): on entry to therapy,
 *    from the Start button or SmartStart, with no card and SCS=1, it clears
 *    any earlier OK (ZKA) and sets ZPK bit 0 again.
 *
 * No RAM, no menu row and no settings slot.
 */

#include "s10_vars.h"

extern int variable_get_by_id(int var_id);
extern void variable_set_by_id(int var_id, int raw_value);

typedef void (*card_check_t)(void *ctx, int present, int keep);
typedef void (*member_set_t)(int index, int bit);

/* Filled in by the patcher. */
extern const card_check_t sd_warning_original;
extern const member_set_t sd_warning_member_set;
extern const unsigned short sd_warning_zpk_index;

enum {
    ZRM_STANDBY = 1,
    ZRM_THERAPY = 4,
    SDS_CARD_PRESENT = 1,
    ZPK_NO_SD_CARD = 0,
};

static int warning_enabled(void)
{
#ifdef VAR_ID_SCS
    return variable_get_by_id(VAR_ID_SCS) == 1;
#else
    return 1;
#endif
}

/* Replaces the stock call in the periodic card check. keep is nonzero
 * when stock would hold the "no SD card" bit (Standby with no card).
 */
void sd_warning_check(void *ctx, int present, int keep)
{
    if (variable_get_by_id(VAR_ID_ZRM) == ZRM_THERAPY)
        keep = 1;
    sd_warning_original(ctx, present, keep);
}

/* Variable callback chained after ZRM's stock callback. */
void sd_warning_mode_changed(void)
{
    if (variable_get_by_id(VAR_ID_ZRM) != ZRM_THERAPY)
        return;
    if (variable_get_by_id(VAR_ID_SDS) & SDS_CARD_PRESENT)
        return;
    if (!warning_enabled())
        return;
    /* An OK pressed at power-up would otherwise keep the message hidden. */
    variable_set_by_id(VAR_ID_ZKA, 0);
    sd_warning_member_set(sd_warning_zpk_index, ZPK_NO_SD_CARD);
}
