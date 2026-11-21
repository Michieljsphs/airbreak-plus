/*
 * sd_warning.c - Show the stock "No SD card detected, press OK to continue"
 * message in Standby as soon as the SD card is removed, and again five
 * minutes after each OK, until the card is back.
 *
 * Stock firmware runs a periodic card check: a virtual method of a small
 * status object, which calls one function (wrapped here) with the card state
 * and whether the device is in Standby. On the first check after power-up a
 * missing card with SCS=1 sets ZPK bit 0, which the Home screen shows as
 * SPM=1; OK sets ZKA. Later checks keep the bit only in Standby without a
 * card and clear it otherwise; a card removed after power-up never sets it.
 *
 * This wrapper raises the message (ZKA=0, ZPK bit 0) when the card goes
 * missing, or when Standby is reached if it went missing during therapy.
 * Once the message has been shown (SPM=1) and dismissed again (SPM no longer
 * 1, which is what OK does), it starts a five minute timer and raises the
 * message again when that expires in Standby (clearing the condition for one
 * check first, so the GUI treats it as new). Inserting the card clears the
 * message, as in stock, and stops the reminders. ZKA is not used to detect
 * OK: the reminder never came when it did.
 *
 * State lives in a heap block that replaces the status object's vtable
 * pointer with a copy of the stock vtable, allocated once on the first check.
 * The status object is a boot-time singleton.
 */

#include "s10_vars.h"

typedef void (*card_check_t)(void *ctx, int present, int standby);
typedef void (*member_set_t)(int index, int bit);
typedef void (*method_t)(void);

/* Filled in by the patcher. */
extern const card_check_t sd_warning_original;
extern const member_set_t sd_warning_member_set;
extern const method_t *const sd_warning_vtable;
extern const unsigned short sd_warning_zpk_index;

extern int variable_get_by_id(int var_id);
extern void variable_set_by_id(int var_id, int raw_value);
extern void variable_set_member_clear_by_index(int index, int bit);
extern void *malloc(unsigned size);
extern void millisecond_timer_init(void *timer, unsigned elapsed);
extern int millisecond_timer_expired(void *timer, unsigned ms);

enum {
    VTABLE_SLOTS = 12,
    STATE_MAGIC = 0x53445741,
    REMIND_AFTER_MS = 5 * 60 * 1000,
    SHOW_TIMEOUT_MS = 10 * 1000,
    SPM_NO_SD_CARD = 1,
    ZPK_NO_SD_CARD = 0,
};

enum phase {
    PHASE_IDLE,      /* card present */
    PHASE_PENDING,   /* raise the message at the next Standby check */
    PHASE_RAISED,    /* raised, waiting for the GUI to show it */
    PHASE_SHOWN,     /* shown, waiting for OK */
    PHASE_TIMING,    /* dismissed, the reminder timer runs */
};

struct state {
    method_t methods[VTABLE_SLOTS];
    unsigned magic;
    unsigned timer[3];
    unsigned char phase;
};

struct status {
    const method_t *vptr;
};

static struct state *state_of(struct status *ctx)
{
    struct state *s = (struct state *)ctx->vptr;
    if (ctx->vptr != sd_warning_vtable)
        return s->magic == STATE_MAGIC ? s : 0;
    if (!(s = malloc(sizeof(*s))))
        return 0;
    for (unsigned i = 0; i < VTABLE_SLOTS; ++i)
        s->methods[i] = sd_warning_vtable[i];
    s->magic = STATE_MAGIC;
    s->phase = PHASE_IDLE;
    ctx->vptr = s->methods;
    return s;
}

static int warning_enabled(void)
{
#ifdef VAR_ID_SCS
    return variable_get_by_id(VAR_ID_SCS) == 1;
#else
    return 1;
#endif
}

static void raise_message(struct state *s)
{
    s->phase = PHASE_RAISED;
    millisecond_timer_init(s->timer, 0);
    /* An earlier OK would keep the message hidden. */
    variable_set_by_id(VAR_ID_ZKA, 0);
    sd_warning_member_set(sd_warning_zpk_index, ZPK_NO_SD_CARD);
}

void sd_warning_check(struct status *ctx, int present, int standby)
{
    struct state *s = state_of(ctx);

    if (s && present) {
        s->phase = PHASE_IDLE;
    } else if (s) {
        if (s->phase == PHASE_IDLE)
            s->phase = PHASE_PENDING;
        if (standby && warning_enabled()) {
            int shown = variable_get_by_id(VAR_ID_SPM) == SPM_NO_SD_CARD;
            switch (s->phase) {
            case PHASE_PENDING:
                raise_message(s);
                break;
            case PHASE_RAISED:
                /* Another message may hold the screen; don't wait forever. */
                if (shown || millisecond_timer_expired(s->timer, SHOW_TIMEOUT_MS))
                    s->phase = PHASE_SHOWN;
                break;
            case PHASE_SHOWN:
                if (!shown) {
                    s->phase = PHASE_TIMING;
                    millisecond_timer_init(s->timer, 0);
                }
                break;
            case PHASE_TIMING:
                /* Clear the condition for one check first, so the GUI
                 * forgets the earlier OK, then raise it again.
                 */
                if (millisecond_timer_expired(s->timer, REMIND_AFTER_MS)) {
                    variable_set_member_clear_by_index(sd_warning_zpk_index,
                                                       ZPK_NO_SD_CARD);
                    s->phase = PHASE_PENDING;
                }
                break;
            }
        }
    }
    sd_warning_original(ctx, present, standby);
}
