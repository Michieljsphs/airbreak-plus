/* Replace Home and on-therapy page titles with the device time, or with a
 * climate readout on Home and the therapy pages.
 */
#include "s10_vars.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define CLOCK_HIDDEN 0xfffu
/* Clock values stay below 24 << 6; climate values are a flagged checksum. */
#define CLIMATE_FLAG 0x800u
#define TITLE_LEN 24
#define TEMP_LEN 8

/* ZML numbers the four therapy pages consecutively on both families. */
#if defined(CDX_VER_0204) || defined(CDX_VER_0205)
#define THERAPY_PAGE_FIRST 9
#else
#define THERAPY_PAGE_FIRST 8
#define HEADER_CLIMATE 1
#endif

struct root_screen {
    u32 methods;
    u32 window_methods;
    u32 window;
    short title;
    /* Stock leaves the two alignment bytes after title unused. */
    u16 clock_time : 12;
    u16 clock_update_count : 4;
};

extern const u16 header_clock_var_id;
extern const u8 header_climate_enabled;
extern const u8 header_climate_always;
extern const u8 header_climate_decimal;
extern const u16 header_climate_var_id;
extern const float header_climate_plate_coupling;
extern const u16 header_climate_coupling_var_id;
extern int variable_get_by_id(int var_id);
extern struct root_screen *gui_root_screen_get(void);
extern struct root_screen *gui_root_screen_init(struct root_screen *root);
extern void gui_root_screen_layout_right_widgets(struct root_screen *root, int changed);
extern void gui_disp_string_at_utf8(const char *text, int x, int y);
extern int gui_get_string_width(const char *text);
extern void WM_InvalidateRect(u32 window, const short *rect);
extern int snprintf(char *buffer, unsigned size, const char *format, ...);

#ifdef HEADER_CLIMATE
#include "humidity_model.h"

/* Display temperature in tenths from a raw degC * 10 value, rounded to whole
 * degrees unless the decimal is shown.
 */
static int display_temp(int raw, int fahrenheit)
{
    if (fahrenheit)
        raw = (raw * 9 + (raw < 0 ? -2 : 2)) / 5 + 320;
    if (!header_climate_decimal)
        raw = (raw + (raw < 0 ? -5 : 5)) / 10 * 10;
    return raw;
}

static void temp_text(char *buffer, int tenths)
{
    const char *sign = tenths < 0 ? "-" : "";
    if (tenths < 0)
        tenths = -tenths;
    if (header_climate_decimal)
        snprintf(buffer, TEMP_LEN, "%s%d.%d", sign, tenths / 10, tenths % 10);
    else
        snprintf(buffer, TEMP_LEN, "%s%d", sign, tenths / 10);
}

/* Tube and plate temperatures and the estimated RH at the tube temperature,
 * using the same chamber estimate as humid_start (humidity_model.h): the room
 * air plus the smaller of the plate and heater-power bounds on the moisture
 * the chamber adds.
 */
static u16 current_climate(char *text)
{
    if (!variable_get_by_id(VAR_ID_HUM))
        return CLOCK_HIDDEN;
    int tube_present = variable_get_by_id(VAR_ID_HTB);
    int fahrenheit = variable_get_by_id(VAR_ID_TMU);
    int ambient_raw = variable_get_by_id(VAR_ID_HAT);
    int plate_raw = variable_get_by_id(VAR_ID_HPT);
    int tube_raw = tube_present ? variable_get_by_id(VAR_ID_HTT) : ambient_raw;

    float coupling = header_climate_coupling_var_id == 0xffffu ?
        header_climate_plate_coupling :
        variable_get_by_id(header_climate_coupling_var_id) * 0.01f;
    struct humidity_coeffs coeffs;
    humidity_coeffs_read(&coeffs);
    float outlet = humidity_outlet(&coeffs,
                                   plate_raw * 0.1f, ambient_raw * 0.1f,
                                   variable_get_by_id(VAR_ID_HAH) * 0.001f,
                                   variable_get_by_id(VAR_ID_HPM) * 0.01f,
                                   variable_get_by_id(VAR_ID_AFL) * 0.01f,
                                   coupling);
    int rh = (int)(100.0f * outlet / ah_sat(tube_raw * 0.1f) + 0.5f);
    if (rh > 100)
        rh = 100;
    if (rh < 0)
        rh = 0;

    int tube = display_temp(tube_raw, fahrenheit);
    int plate = display_temp(plate_raw, fahrenheit);
    if (text) {
        char tube_text[TEMP_LEN], plate_text[TEMP_LEN];
        temp_text(tube_text, tube);
        temp_text(plate_text, plate);
        snprintf(text, TITLE_LEN, "%s\xc2\xb0 %s\xc2\xb0 %d%%",
                 tube_present ? tube_text : "--", plate_text, rh);
    }
    unsigned sum = ((unsigned)tube * 31u + (unsigned)plate * 7u + (unsigned)rh +
                    (tube_present ? 0x400u : 0u)) & 0x7ffu;
    return CLIMATE_FLAG | (sum == 0x7ffu ? 0u : sum);
}
#endif

/* Returns the title value for this page and formats it when text is set. */
static u16 current_title(const struct root_screen *root, char *text)
{
    unsigned page = variable_get_by_id(VAR_ID_ZML);
    int home = page == 0 && root->title == 0;
    int therapy = page >= THERAPY_PAGE_FIRST && page < THERAPY_PAGE_FIRST + 4 &&
                  (root->title == 7 || root->title == 8);
#ifdef HEADER_CLIMATE
    if ((home || therapy || header_climate_always) && header_climate_enabled &&
            (header_climate_var_id == 0xffffu ||
             variable_get_by_id(header_climate_var_id))) {
        u16 climate = current_climate(text);
        if (climate != CLOCK_HIDDEN)
            return climate;
    }
#endif
    if (!(home || therapy))
        return CLOCK_HIDDEN;
    if (header_clock_var_id != 0xffffu &&
            !variable_get_by_id(header_clock_var_id))
        return CLOCK_HIDDEN;

    unsigned time = variable_get_by_id(VAR_ID_TIC);
    unsigned hour = (time >> 16) & 0xff;
    unsigned minute = (time >> 8) & 0xff;
    if (hour >= 24 || minute >= 60)
        return CLOCK_HIDDEN;
    if (text)
        snprintf(text, 6, "%02u:%02u", hour, minute);
    return (hour << 6) | minute;
}

/* Only the first title draw call is hooked; page numbers and icons stay stock. */
void start(const char *text, int x, int y)
{
    struct root_screen *root = gui_root_screen_get();
    char title[TITLE_LEN];
    root->clock_time = current_title(root, title);
    if (root->clock_time != CLOCK_HIDDEN)
        text = title;
    gui_disp_string_at_utf8(text, x, y);
}

#ifdef HEADER_CLIMATE
/* The page counter ("1/3") drawn after the title: left out beside the climate
 * readout, which needs the room. The title draw above runs first.
 */
void header_climate_page_draw(const char *text, int x, int y)
{
    struct root_screen *root = gui_root_screen_get();
    if (root->clock_time != CLOCK_HIDDEN && (root->clock_time & CLIMATE_FLAG))
        return;
    gui_disp_string_at_utf8(text, x, y);
}
#endif

int header_clock_title_width(const char *text)
{
    struct root_screen *root = gui_root_screen_get();
    char title[TITLE_LEN];
    if (root->clock_time != CLOCK_HIDDEN &&
            current_title(root, title) != CLOCK_HIDDEN)
        text = title;
    return gui_get_string_width(text);
}

struct root_screen *header_clock_init(struct root_screen *root)
{
    gui_root_screen_init(root);
    root->clock_time = CLOCK_HIDDEN;
    root->clock_update_count = 0;
    return root;
}

#ifdef HEADER_CLIMATE
#include "ram_pointers.h"

/* Climate timer: Accessories Off/On switches with a time each, in hours
 * (raw = hours * 100 in quarter-hour steps).
 *  - Warm-up at: on Home, out of therapy, with a humidifier attached, set WUP
 *    (the Run Warmup button) once in the WARMUP_WINDOW minutes after the time.
 *  - Humid. off at: once per therapy session, within HUMID_OFF_WINDOW minutes
 *    after the time, switch Humidity Enable (HMX) off so the heated tube dries
 *    with dry air; switch it back on when that therapy session ends.
 * The state lives in RAM, so a power cut while the humidifier is switched off
 * leaves HMX off.
 */
#define WARMUP_WINDOW 10u
#define HUMID_OFF_WINDOW (4u * 60u)
#define MINUTES_PER_DAY (24u * 60u)
#define WUP_RUNNING 1
#define CLIMATE_TIMER_MAGIC 0x434c544du

struct climate_timer {
    unsigned magic;
    u8 warmup_fired;
    u8 off_done;
    u8 switched_off;
    u8 therapy;
};

extern const u16 climate_timer_warmup_enable_var_id;
extern const u16 climate_timer_warmup_var_id;
extern const u16 climate_timer_humid_off_enable_var_id;
extern const u16 climate_timer_humid_off_var_id;
extern int ivars[];
extern void *malloc(unsigned size);
extern void variable_set_by_id(int var_id, int value);

/* Same table as common_code.c get_pointer, which may run first or not at all. */
static struct climate_timer *climate_timer_state(void)
{
    magic_ptr_t *slot = &magic_ptr;
    if (slot->magic != RAM_POINTERS_MAGIC) {
        void **pointers = malloc(sizeof(void *) * __PTR_LAST);
        if (!pointers)
            return 0;
        for (unsigned i = 0; i < __PTR_LAST; ++i)
            pointers[i] = 0;
        slot->pointers = pointers;
        slot->magic = RAM_POINTERS_MAGIC;
    }
    struct climate_timer *s = slot->pointers[PTR_CLIMATE_TIMER];
    if (!s) {
        if (!(s = malloc(sizeof(*s))))
            return 0;
        s->magic = CLIMATE_TIMER_MAGIC;
        s->warmup_fired = 0;
        s->off_done = 0;
        s->switched_off = 0;
        s->therapy = 0;
        slot->pointers[PTR_CLIMATE_TIMER] = s;
    }
    return s->magic == CLIMATE_TIMER_MAGIC ? s : 0;
}

/* Timer time in minutes of the day, or -1 when unassigned or switched Off. */
static int timer_minutes(u16 enable_id, u16 time_id)
{
    if (enable_id == 0xffffu || time_id == 0xffffu || !variable_get_by_id(enable_id))
        return -1;
    int raw = variable_get_by_id(time_id);
    if (raw < 0 || raw >= 2400)
        return -1;
    return raw * 3 / 5;
}

static int in_window(unsigned now, int start, unsigned length)
{
    return start >= 0 && (now + MINUTES_PER_DAY - (unsigned)start) % MINUTES_PER_DAY < length;
}

static void climate_timer(void)
{
    int warmup_at = timer_minutes(climate_timer_warmup_enable_var_id,
                                  climate_timer_warmup_var_id);
    int off_at = timer_minutes(climate_timer_humid_off_enable_var_id,
                               climate_timer_humid_off_var_id);
    if (warmup_at < 0 && off_at < 0)
        return;
    struct climate_timer *s = climate_timer_state();
    if (!s)
        return;

    unsigned time = variable_get_by_id(VAR_ID_TIC);
    unsigned hour = (time >> 16) & 0xff, minute = (time >> 8) & 0xff;
    if (hour >= 24 || minute >= 60)
        return;
    unsigned now = hour * 60 + minute;
    int therapy = ivars[0x6f] != 0;
    int humidifier = variable_get_by_id(VAR_ID_HUM) != 0;

    if (therapy != s->therapy) {
        s->therapy = therapy;
        s->off_done = 0;
        if (!therapy && s->switched_off) {
            s->switched_off = 0;
            if (!variable_get_by_id(VAR_ID_HMX))
                variable_set_by_id(VAR_ID_HMX, 1);
        }
    }

    if (!in_window(now, warmup_at, WARMUP_WINDOW)) {
        s->warmup_fired = 0;
    } else if (!s->warmup_fired && !therapy && humidifier &&
               variable_get_by_id(VAR_ID_ZML) == 0) {
        s->warmup_fired = 1;
        if (variable_get_by_id(VAR_ID_WUP) != WUP_RUNNING)
            variable_set_by_id(VAR_ID_WUP, WUP_RUNNING);
    }

    if (therapy && !s->off_done && in_window(now, off_at, HUMID_OFF_WINDOW)) {
        s->off_done = 1;
        if (humidifier && variable_get_by_id(VAR_ID_HMX)) {
            variable_set_by_id(VAR_ID_HMX, 0);
            s->switched_off = 1;
        }
    }
}
#endif

void header_clock_update(struct root_screen *root, int changed)
{
    gui_root_screen_layout_right_widgets(root, changed);
    if (++root->clock_update_count < 10)
        return;
    root->clock_update_count = 0;
#ifdef HEADER_CLIMATE
    climate_timer();
#endif
    u16 value = current_title(root, 0);
    if (value != root->clock_time) {
        const short rect[4] = {0, 0, 239, 32};
        root->clock_time = value;
        WM_InvalidateRect(root->window, rect);
    }
}
