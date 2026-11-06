/* Replace Home and on-therapy page titles with the device time. */
#include "s10_vars.h"

typedef unsigned short u16;
typedef unsigned int u32;

#define CLOCK_HIDDEN 0xfffu

/* ZML numbers the four therapy pages consecutively on both families. */
#if defined(CDX_VER_0204) || defined(CDX_VER_0205)
#define THERAPY_PAGE_FIRST 9
#else
#define THERAPY_PAGE_FIRST 8
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
extern int variable_get_by_id(int var_id);
extern struct root_screen *gui_root_screen_get(void);
extern struct root_screen *gui_root_screen_init(struct root_screen *root);
extern void gui_root_screen_layout_right_widgets(struct root_screen *root, int changed);
extern void gui_disp_string_at_utf8(const char *text, int x, int y);
extern int gui_get_string_width(const char *text);
extern void WM_InvalidateRect(u32 window, const short *rect);
extern int snprintf(char *buffer, unsigned size, const char *format, ...);

static u16 current_clock(const struct root_screen *root)
{
    if (header_clock_var_id != 0xffffu &&
            !variable_get_by_id(header_clock_var_id))
        return CLOCK_HIDDEN;

    unsigned page = variable_get_by_id(VAR_ID_ZML);
    if (!((page == 0 && root->title == 0) ||
          (page >= THERAPY_PAGE_FIRST && page < THERAPY_PAGE_FIRST + 4 &&
           (root->title == 7 || root->title == 8))))
        return CLOCK_HIDDEN;

    unsigned time = variable_get_by_id(VAR_ID_TIC);
    unsigned hour = (time >> 16) & 0xff;
    unsigned minute = (time >> 8) & 0xff;
    if (hour >= 24 || minute >= 60)
        return CLOCK_HIDDEN;
    return (hour << 6) | minute;
}

static void format_clock(char *text, unsigned time)
{
    snprintf(text, 6, "%02u:%02u", time >> 6, time & 0x3f);
}

/* Only the first title draw call is hooked; page numbers and icons stay stock. */
void start(const char *text, int x, int y)
{
    struct root_screen *root = gui_root_screen_get();
    char clock[6];
    root->clock_time = current_clock(root);
    if (root->clock_time != CLOCK_HIDDEN) {
        format_clock(clock, root->clock_time);
        text = clock;
    }
    gui_disp_string_at_utf8(text, x, y);
}

int header_clock_title_width(const char *text)
{
    struct root_screen *root = gui_root_screen_get();
    char clock[6];
    if (root->clock_time != CLOCK_HIDDEN) {
        format_clock(clock, root->clock_time);
        text = clock;
    }
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
    u16 time = current_clock(root);
    if (time != root->clock_time) {
        const short rect[4] = {0, 0, 239, 32};
        root->clock_time = time;
        WM_InvalidateRect(root->window, rect);
    }
}
