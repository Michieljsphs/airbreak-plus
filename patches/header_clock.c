/* Replace Home and on-therapy page titles with the device time, or with a
 * climate readout on the therapy pages.
 */
#include "s10_vars.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define CLOCK_HIDDEN 0xfffu
/* Clock values stay below 24 << 6; climate values are a flagged checksum. */
#define CLIMATE_FLAG 0x800u
#define TITLE_LEN 24

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
extern float expf(float x);

/* Magnus saturation vapour pressure over water, hPa. */
static float saturation(float t)
{
    return 6.112f * expf(17.62f * t / (243.12f + t));
}

/* Rounded display temperature from a raw degC * 10 value. */
static int display_temp(int raw, int fahrenheit)
{
    if (fahrenheit)
        raw = raw * 9 / 5 + 320;
    return (raw + (raw < 0 ? -5 : 5)) / 10;
}

/* Tube and plate temperatures and the estimated RH at the tube temperature,
 * using the same chamber estimate as tube_warmup: outlet air saturated at
 * ambient + coupling * (plate - ambient), never drier than the room.
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
    float ambient = ambient_raw * 0.1f;
    float outlet = ambient + coupling * (plate_raw * 0.1f - ambient);
    float vapour = saturation(outlet);
    float room = variable_get_by_id(VAR_ID_HAH) * 0.001f * saturation(ambient);
    if (vapour < room)
        vapour = room;
    int rh = (int)(100.0f * vapour / saturation(tube_raw * 0.1f) + 0.5f);
    if (rh > 100)
        rh = 100;
    if (rh < 0)
        rh = 0;

    int tube = display_temp(tube_raw, fahrenheit);
    int plate = display_temp(plate_raw, fahrenheit);
    if (text) {
        if (tube_present)
            snprintf(text, TITLE_LEN, "%d\xc2\xb0 %d\xc2\xb0 %d%%", tube, plate, rh);
        else
            snprintf(text, TITLE_LEN, "--\xc2\xb0 %d\xc2\xb0 %d%%", plate, rh);
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
    if (therapy && header_climate_enabled) {
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

void header_clock_update(struct root_screen *root, int changed)
{
    gui_root_screen_layout_right_widgets(root, changed);
    if (++root->clock_update_count < 10)
        return;
    root->clock_update_count = 0;
    u16 value = current_title(root, 0);
    if (value != root->clock_time) {
        const short rect[4] = {0, 0, 239, 32};
        root->clock_time = value;
        WM_InvalidateRect(root->window, rect);
    }
}
