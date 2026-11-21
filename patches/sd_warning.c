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
 * Once OK is pressed it starts a five minute timer and raises the message
 * again when it expires in Standby. Inserting the card clears the message,
 * as in stock, and stops the reminders.
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
extern void *malloc(unsigned size);
extern void millisecond_timer_init(void *timer, unsigned elapsed);
extern int millisecond_timer_expired(void *timer, unsigned ms);

enum {
    VTABLE_SLOTS = 12,
    STATE_MAGIC = 0x53445741,
    REMIND_AFTER_MS = 5 * 60 * 1000,
    ZPK_NO_SD_CARD = 0,
};

struct state {
    method_t methods[VTABLE_SLOTS];
    unsigned magic;
    unsigned timer[3];
    unsigned char absent;
    unsigned char pending;   /* raise the message at the next Standby check */
    unsigned char timing;    /* OK pressed, the reminder timer runs */
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
    s->absent = 0;
    s->pending = 0;
    s->timing = 0;
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
    s->pending = 0;
    s->timing = 0;
    /* An earlier OK would keep the message hidden. */
    variable_set_by_id(VAR_ID_ZKA, 0);
    sd_warning_member_set(sd_warning_zpk_index, ZPK_NO_SD_CARD);
}

void sd_warning_check(struct status *ctx, int present, int standby)
{
    struct state *s = state_of(ctx);

    if (s && present) {
        s->absent = 0;
    } else if (s) {
        if (!s->absent) {
            s->absent = 1;
            s->pending = 1;
            s->timing = 0;
        }
        if (standby && warning_enabled()) {
            if (s->pending) {
                raise_message(s);
            } else if (variable_get_by_id(VAR_ID_ZKA) == 1) {
                if (!s->timing) {
                    s->timing = 1;
                    millisecond_timer_init(s->timer, 0);
                } else if (millisecond_timer_expired(s->timer, REMIND_AFTER_MS)) {
                    raise_message(s);
                }
            }
        }
    }
    sd_warning_original(ctx, present, standby);
}
