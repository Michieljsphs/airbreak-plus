/* Graph rendering for the therapy-screen payload. */
#include "stubs.h"
#include "common_code.h"
#include "therapy_screen.h"
#include "therapy_worker.h"
#include "s10_vars.h"
#include "humidity_model.h"

typedef struct {
	uint32 vtable;
	uint32 hwin;
	int position;
	int last_pos_x;
} graph_widget_t;

typedef int (*graph_draw_t)(graph_widget_t *obj);
typedef int (*graph_update_t)(graph_widget_t *obj, int new_position);
typedef void (*graph_header_update_t)(void *obj);
typedef void (*graph_numbers_update_t)(void *obj, int left_var, int right_var);

extern const uint32 graph_draw_original;
extern const uint32 graph_update_original;
extern const uint32 graph_header_update_original;
extern const uint32 graph_numbers_update_original;
extern const uint32 graph_simple_draw_original;
extern const uint32 graph_simple_update_original;
extern int gui_base_window_y0(void *obj);
extern int gui_base_window_height(void *obj);
extern void gui_invalidate_window(void *obj);
extern void gui_method_fc104_setup_and_invalidate(
	uint32 hwin, int left, int top, int right, int bottom);
extern void memcpy_unrolled(void *destination, const void *source,
			    unsigned size);

/* The stock constructor initializes the first 12 bytes after allocation. */
graph_widget_t *MAIN graph_widget_alloc(void) {
	graph_widget_t *obj = malloc(sizeof(*obj));
	if (obj)
		obj->last_pos_x = -1;
	return obj;
}

STATIC bool graph_enabled(void) {
	return therapy_screen_has_graph(therapy_screen_get_choice());
}

/* The native round gauge occupies the first 124 bytes. */
typedef struct {
	uint8 native[124];
	int last_pos_x, top;
} graph_simple_t;

graph_simple_t *MAIN graph_simple_alloc(void) {
	graph_simple_t *obj = malloc(sizeof(*obj));
	if (obj) { obj->last_pos_x = -1; obj->top = -1; }
	return obj;
}

/* The optional keep-screen-on patch registers this with keep_backlight. */
int MAIN graph_keep_backlight_requested(void) {
	return *therapy_mode != MODE_OFF && graph_enabled();
}

STATIC void LCD_FillRect2(int x1, int y1, int x2, int y2) {
	int temp = 0;
	if (y1 > y2) { temp = y2; y2 = y1; y1 = temp; }
	if (x1 > x2) { temp = x2; x2 = x1; x1 = temp; }
	LCD_FillRect(x1, y1, x2, y2);
}

STATIC void LCD_FillRect_Alt(int x, int y, int w, int h) {
	LCD_FillRect2(x, y, x+w-1, y+h-1);
}

// The parent also calls this draw method when only the numeric row is invalidated.
STATIC bool graph_widget_is_invalidated(void) {
	const short * const clip = (short*)(gui_context + 8);
	const short xOff = *(short*)(gui_context + 76);
	const short yOff = *(short*)(gui_context + 78);
	const int left = xOff + 0x39;
	const int top = yOff;
	const int right = xOff + 0xd5;
	const int bottom = yOff + 0x0e;

	return clip[0] <= right && clip[2] >= left &&
	       clip[1] <= bottom && clip[3] >= top;
}

STATIC bool fixed_string_changed(const uint8 *before, const uint8 *after,
				 unsigned size) {
	for (unsigned i = 0; i < size; i++) {
		if (before[i] != after[i])
			return true;
		if (before[i] == 0)
			return false;
	}
	return false;
}

STATIC bool bytes_changed(const uint8 *before, const uint8 *after,
			  unsigned size) {
	for (unsigned i = 0; i < size; i++) {
		if (before[i] != after[i])
			return true;
	}
	return false;
}

/* Climate chart, instead of the flow and pressure trace when patching with
 * --patch-graph-climate: the plate and tube temperatures and the estimated RH
 * (humidity_model.h) over the first 60 minutes of therapy, or the width set
 * with --graph-width or the Graph Width setting, one column per
 * width / 240. Dashed lines show the tube setting, the Humid Start Min Tube
 * and the target RH. It fills the strip of the trace, and stops when full so
 * it is still there in the morning.
 */
#define CLIMATE_COLUMNS 240
/* The therapy timer counts 10 ms ticks, 6000 a minute. */
#define TICKS_PER_MINUTE 6000
#define MIN_WIDTH_MINUTES 10
#define MAX_WIDTH_MINUTES 720
/* A new night: the chart was not updated for this long, while the therapy
 * timer advanced by less than SESSION_TIMER_TICKS. Within one night the
 * timer runs on through such a gap (a menu, the screen off); between two
 * nights it stood still while therapy was off.
 */
#define SESSION_GAP_MS 1800000
#define SESSION_TIMER_TICKS 30000

/* Series, also the scale of a setpoint line. Values are 0 for none,
 * temperatures in half degrees C, RH in percent.
 */
enum { PLATE, TUBE, RH };

/* One chart for every therapy-screen widget. It is a node of the stock
 * therapy-worker list, which keeps it findable, like the breath tracking.
 */
typedef struct {
	therapy_worker node;
	/* Therapy timer at the last update; it going back is a new night. */
	unsigned last_therapy;
	/* Therapy timer at the start of this night: it is not restarted at zero
	 * for each night.
	 */
	unsigned session_start;
	/* Time since the last update, for a gap between nights. */
	unsigned seen[3];
	/* Column of the therapy time at the last update; keeps counting after
	 * the chart is full.
	 */
	unsigned column;
	/* Strip the chart was last drawn in. */
	short top, bottom;
	/* Chart width in minutes at the last update, and its grid spacing. */
	unsigned short width;
	unsigned short columns_per_tick;
	uint8 active;
	/* A Home hold asked for a new chart starting now. */
	uint8 reset;
	/* Setpoint lines, 0 when not shown. */
	uint8 tube_set;
	uint8 rh_set;
	uint8 min_tube;
	uint8 data[3][CLIMATE_COLUMNS];
} climate_state_t;

extern int variable_get_by_id(int var_id);
extern void millisecond_timer_init(void *timer, unsigned elapsed);
extern int millisecond_timer_expired(void *timer, unsigned ms);
extern float climate_target_rh_get(void);
extern const uint8 graph_climate;
extern const uint32 graph_width_minutes;
extern const unsigned short graph_width_var_id;
extern const unsigned short graph_humid_start_var_id;
extern const unsigned short graph_min_tube_c;
extern const unsigned short graph_coupling_var_id;

STATIC void climate_noop(therapy_worker *node) { }

static const therapy_worker_method climate_methods[7] = {
	climate_noop, climate_noop, climate_noop, climate_noop,
	climate_noop, climate_noop, climate_noop
};

STATIC unsigned irq_save(void) {
	unsigned saved;
	__asm__ volatile ("mrs %0, primask\n\tcpsid i" : "=r" (saved) :: "memory");
	return saved;
}

STATIC void irq_restore(unsigned saved) {
	__asm__ volatile ("msr primask, %0" :: "r" (saved) : "memory");
}

/* The chart, made on first use; NULL when the heap has no room. */
STATIC climate_state_t *climate_state(void) {
	climate_state_t *s = (climate_state_t *)therapy_worker_find(2, climate_methods);
	if (s)
		return s;
	s = malloc(sizeof(*s));
	if (!s)
		return 0;
	uint8 *p = (uint8 *)s;
	for (unsigned i = 0; i < sizeof(*s); i++)
		p[i] = 0;
	/* The stock append links the node enabled, with the base worker methods,
	 * whose unimplemented slots abort. This runs in the screen task while the
	 * therapy task may be walking the list, so the node must not be seen
	 * before it has the chart's own methods.
	 */
	unsigned saved = irq_save();
	therapy_worker_append(&s->node, 2);
	s->node.methods = climate_methods;
	s->node.enabled = 0;
	irq_restore(saved);
	millisecond_timer_init(s->seen, 0);
	s->columns_per_tick = 40;
	return s;
}

/* GUI colors are 0xBBGGRR. */
#define RGB(r, g, b) (((b) << 16) | ((g) << 8) | (r))
#define COLOR_PLATE RGB(0xff, 0x30, 0x30)
#define COLOR_TUBE RGB(0xff, 0x99, 0x00)
#define COLOR_RH RGB(0x40, 0x99, 0xff)
#define COLOR_TUBE_SET RGB(0xb0, 0x6a, 0x00)
#define COLOR_RH_SET RGB(0x2c, 0x6a, 0xb0)
#define COLOR_GRID RGB(0x30, 0x30, 0x30)

/* y of a value in the strip: temperatures on a 15-71 degC axis (v in half
 * degrees), RH on a 20-100 % axis, both over the full strip height.
 */
STATIC int value_y(const climate_state_t *s, int series, int v) {
	const int height = s->bottom - s->top;
	int y = series == RH ? s->bottom - (v - 20) * height / 80 :
			       s->bottom - (v - 30) * height / 112;
	return clamp(y, (int)s->top, (int)s->bottom);
}

/* Raw degC * 10 to half degrees, 1..255 (0 means no data). */
STATIC int half_degrees(int raw) {
	return clamp((raw + 2) / 5, 1, 255);
}

STATIC float coupling(void) {
	if (graph_coupling_var_id == 0xffffu)
		return HUMIDITY_PLATE_COUPLING;
	return coupling_from_raw(variable_get_by_id(graph_coupling_var_id));
}

/* The time the chart covers, in minutes. */
STATIC unsigned chart_width_minutes(void) {
	int minutes = graph_width_minutes;
	if (graph_width_var_id != 0xffffu)
		minutes = variable_get_by_id(graph_width_var_id) * 6;
	return clamp(minutes, MIN_WIDTH_MINUTES, MAX_WIDTH_MINUTES);
}

/* Tube setting and Min Tube (0 when not shown), and the target RH. */
STATIC void climate_setpoints(climate_state_t *s) {
	int hts = 0, floor = 0;
	if (variable_get_by_id(VAR_ID_HTB)) {
		hts = half_degrees(variable_get_by_id(VAR_ID_HTS));
		if (graph_humid_start_var_id == 0xffffu ||
		    variable_get_by_id(graph_humid_start_var_id))
			floor = graph_min_tube_c * 2;
		if (floor <= 32 || floor >= hts)
			floor = 0;
	}
	s->tube_set = hts;
	s->min_tube = floor;
	s->rh_set = (int)(climate_target_rh_get() * 100.0f + 0.5f);
}

/* Plate and tube temperatures and the estimated RH at the tube temperature,
 * the same estimate as the header climate readout.
 */
STATIC void climate_sample(climate_state_t *s, int x) {
	static const unsigned short ids[] = {
		VAR_ID_HUM, VAR_ID_HTB, VAR_ID_HAT, VAR_ID_HPT, VAR_ID_HTT,
		VAR_ID_HAH, VAR_ID_HPM, VAR_ID_AFL,
	};
	int v[8];
	for (int i = 0; i < 8; i++)
		v[i] = variable_get_by_id(ids[i]);
	enum { HUM, HTB, HAT, HPT, HTT, HAH, HPM, AFL };
	if (!v[HUM])
		return;
	if (!v[HTB])
		v[HTT] = v[HAT];
	struct humidity_coeffs coeffs;
	humidity_coeffs_read(&coeffs);
	float outlet = humidity_outlet(&coeffs, v[HPT] * 0.1f, v[HAT] * 0.1f,
				       v[HAH] * 0.001f, v[HPM] * 0.01f,
				       v[AFL] * 0.01f, coupling());
	int rh = (int)(100.0f * outlet / ah_sat(v[HTT] * 0.1f) + 0.5f);
	s->data[PLATE][x] = half_degrees(v[HPT]);
	s->data[TUBE][x] = v[HTB] ? half_degrees(v[HTT]) : 0;
	s->data[RH][x] = clamp(rh, 1, 100);
}

/* A horizontal setpoint line, drawn in column x: dashed, or dotted. */
STATIC void climate_dash(const climate_state_t *s, int series, int v, int x,
			 int pattern) {
	if (v && (x & pattern) == 0) {
		int y = value_y(s, series, v);
		LCD_FillRect(x, y, x, y);
	}
}

STATIC void climate_draw_column(const climate_state_t *s, int x) {
	static const unsigned colors[3] = { COLOR_PLATE, COLOR_TUBE, COLOR_RH };

	GUI_SetColor(0x000000);
	LCD_FillRect(x, s->top - 1, x, s->bottom);

	// faint grid every 10 degC and a tick at the grid spacing
	GUI_SetColor(COLOR_GRID);
	for (int t = 40; t <= 140; t += 20)
		climate_dash(s, TUBE, t, x, 3);
	if (x % s->columns_per_tick == 0)
		LCD_FillRect(x, s->bottom - 3, x, s->bottom);

	// tube setting and target RH dashed, Min Tube dotted
	GUI_SetColor(COLOR_TUBE_SET);
	climate_dash(s, TUBE, s->tube_set, x, 4);
	climate_dash(s, TUBE, s->min_tube, x, 3);
	GUI_SetColor(COLOR_RH_SET);
	climate_dash(s, RH, s->rh_set, x, 4);

	// plate, then RH, then the tube on top: 2 px lines joined to the
	// previous column
	for (int i = 0; i < 3; i++) {
		int series = i == 0 ? PLATE : i == 1 ? RH : TUBE;
		const uint8 *v = s->data[series];
		if (!v[x])
			continue;
		int y = value_y(s, series, v[x]);
		int y0 = x > 0 && v[x - 1] ? value_y(s, series, v[x - 1]) : y;
		GUI_SetColor(colors[series]);
		LCD_FillRect(x, min(y, y0), x, min(max(y, y0) + 1, (int)s->bottom));
	}
}

/* Samples a column every width / 240 of therapy time (15 s for an hour) for
 * the chart width, and redraws the whole chart on a new column or after a
 * repaint; a new night clears the chart. Columns are counted from the
 * therapy timer, so the chart starts at the start of therapy even when the
 * therapy screen was not shown then.
 *
 * The therapy screen, and with it this update, is gone while therapy is off,
 * so the stop is rarely seen here. A new night is the timer going back, or
 * a gap of 30 minutes since the last update over which the timer hardly
 * advanced: it then stood still while therapy was off, and the night started
 * where it stopped. A Home hold or a new width starts the chart over from
 * now.
 */
STATIC int climate_column(int *last_pos_x, bool only_if_new, int top, int bottom) {
	climate_state_t *s = climate_state();
	if (!s)
		return 0;
	if (*therapy_mode == MODE_OFF) {
		s->active = 0;
		*last_pos_x = -1;
		return 0;
	}
	unsigned therapy = *pap_timer;
	bool all = !only_if_new;
	bool manual = s->reset;
	s->reset = 0;
	/* A new width changes what every column means: start the chart over. */
	unsigned width = chart_width_minutes();
	if (s->active && s->width && (unsigned)s->width != width)
		manual = true;
	s->width = width;
	unsigned tick_minutes = width <= 120 ? 10 : width <= 360 ? 30 : 60;
	s->columns_per_tick = max(1u, tick_minutes * CLIMATE_COLUMNS / width);
	if (top != s->top || bottom != s->bottom) {
		s->top = top;
		s->bottom = bottom;
		all = true;
	}

	bool gap = millisecond_timer_expired(s->seen, SESSION_GAP_MS);
	millisecond_timer_init(s->seen, 0);
	bool restarted = therapy < s->last_therapy;
	if (!s->active || restarted || manual ||
	    (gap && therapy - s->last_therapy < SESSION_TIMER_TICKS)) {
		/* From zero after a restart, from now after a manual restart,
		 * else from where the timer stopped at the end of the last
		 * night (zero after power-up).
		 */
		s->session_start = restarted ? 0 : manual ? therapy : s->last_therapy;
		uint8 *p = &s->data[0][0];
		for (unsigned i = 0; i < sizeof(s->data); i++)
			p[i] = 0;
		s->active = 1;
		s->column = ~0u;
		all = true;
	}
	s->last_therapy = therapy;

	/* Setpoints are checked once a column, also when the chart is full. */
	unsigned column = (therapy - s->session_start) /
			  (width * (TICKS_PER_MINUTE / CLIMATE_COLUMNS));
	if (column != s->column) {
		s->column = column;
		climate_setpoints(s);
		if (column < CLIMATE_COLUMNS)
			climate_sample(s, column);
		all = true;
	}
	if (!all)
		return 0;
	*last_pos_x = column;

	// Draw within the strip, outside the native widget's rectangle.
	unsigned * const color_ptr = (unsigned*)(gui_context + 60);
	short * const clip = (short*)(gui_context + 8);
	short * const xOff = (short*)(gui_context + 76);
	short * const yOff = (short*)(gui_context + 78);
	const short old_clip[4] = { clip[0], clip[1], clip[2], clip[3] };
	const short old_xOff = *xOff;
	const short old_yOff = *yOff;
	const unsigned old_color = *color_ptr;
	clip[0] = 0;
	clip[1] = top - 1;
	clip[2] = 239;
	clip[3] = bottom + 1;
	*xOff = 0;
	*yOff = 0;
	for (int x = 0; x < CLIMATE_COLUMNS; x++)
		climate_draw_column(s, x);
	clip[0] = old_clip[0];
	clip[1] = old_clip[1];
	clip[2] = old_clip[2];
	clip[3] = old_clip[3];
	*xOff = old_xOff;
	*yOff = old_yOff;
	*color_ptr = old_color;
	return 1;
}

/* button_hooks calls this in the GUI task when Home has been held for 5 s.
 * During therapy with the climate chart it clears the chart and starts it
 * again from now; otherwise the hold keeps its stock behaviour.
 */
int MAIN graph_chart_reset(unsigned event, unsigned held_ms) {
	if (!graph_climate || *therapy_mode == MODE_OFF || !graph_enabled())
		return 0;
	climate_state_t *s = climate_state();
	if (!s)
		return 0;
	s->reset = 1;
	return 1;
}

STATIC int graph_draw_column(int *last_pos_x, bool only_if_new, int top, int bottom) {
	if (graph_climate)
		return climate_column(last_pos_x, only_if_new, top, bottom);

	// don't do anything if we are not in an active therapy mode
	if (*therapy_mode == 0) {
		*last_pos_x = -1;
		return 0;
	}

	const unsigned pos_x = (*pap_timer / 15) % 240; // ~6.66px per second (unit of timer is 10ms)
	if (only_if_new && *last_pos_x == (int)pos_x) return 0;
	*last_pos_x = pos_x;

	// Draw within the graph strip, outside the native widget's rectangle.
	unsigned * const color_ptr = (unsigned*)(gui_context + 60);
	short * const clip = (short*)(gui_context + 8);
	short * const xOff = (short*)(gui_context + 76);
	short * const yOff = (short*)(gui_context + 78);
	const short old_x0 = clip[0];
	const short old_y0 = clip[1];
	const short old_x1 = clip[2];
	const short old_y1 = clip[3];
	const short old_xOff = *xOff;
	const short old_yOff = *yOff;
	const unsigned old_color = *color_ptr;
	clip[0] = 0;
	clip[1] = top - 1;
	clip[2] = 239;
	clip[3] = bottom + 1;
	*xOff = 0;
	*yOff = 0;

	// Draw a strip chart
	const int height_pres = (bottom - top) / 2;
	const int height_flow = bottom - top - height_pres;

	int command = p_command * height_pres / 20.0f;
	int error = -p_error * height_pres * 0.15f;

	GUI_SetColor(0x000000);
	LCD_FillRect(pos_x, top - 1, pos_x + 11, bottom + 1);
	if (breath_progress > 0.0f && breath_progress <= 0.5f) { // Active inhale
		GUI_SetColor(0x101010);
		LCD_FillRect(pos_x, top - 1, pos_x + 1, bottom + 1);
	}

	float g_bottom = bottom;

	GUI_SetColor(0x202020);
	for(int i=1; i<=4; i++) { // draw 0, 5, 10, 15, 20 very faintly
		LCD_FillRect_Alt(pos_x, g_bottom - i*height_pres/4, 2, 1);
	}
	// draw amplified pressure error with respect to the commanded pressure
	GUI_SetColor(0x000080);
	LCD_FillRect2(pos_x, g_bottom - command, pos_x + 1, g_bottom - command + error);
	// draw the current commanded pressure
	GUI_SetColor(0x00FF88);
	LCD_FillRect_Alt(pos_x, g_bottom - command, 2, 1);

	float g_top = top;

	// Draw the leak-compensated flow variable as solid bars
	GUI_SetColor(0xFF8800);
	const int g_center = g_top + height_flow/2;
	LCD_FillRect2(pos_x, g_center, pos_x+1, g_center - clamp(*flow_compensated * height_flow / 80, -height_flow/2, height_flow/2));
	GUI_SetColor(0x000000);
	LCD_FillRect_Alt(pos_x, g_top + height_flow/2, 2, 1);

	GUI_SetColor(0x666666);
	LCD_FillRect_Alt(pos_x, top, 4, 1);
	LCD_FillRect_Alt(pos_x, top + height_flow, 4, 1);
	LCD_FillRect_Alt(pos_x, bottom, 4, 1);

	// restore the old clipping rectangle
	clip[0] = old_x0;
	clip[1] = old_y0;
	clip[2] = old_x1;
	clip[3] = old_y1;
	*xOff = old_xOff;
	*yOff = old_yOff;
	*color_ptr = old_color;

	return 1;
}

// Keep the pressure bar visible alongside the graph below it.
int MAIN graph_widget_draw(graph_widget_t *obj) {
	int result = ((graph_draw_t)graph_draw_original)(obj);
	if (!graph_enabled()) {
		obj->last_pos_x = -1;
		return result;
	}
	if (graph_widget_is_invalidated())
		graph_draw_column(&obj->last_pos_x, false, 155, 235);
	return result;
}

// The stock update method only redraws when the quantized bar position changes.
// Keep that invalidation and advance the graph independently on each new column.
int MAIN graph_widget_update(graph_widget_t *obj, int new_position) {
	int result = ((graph_update_t)graph_update_original)(obj, new_position);
	if (!graph_enabled()) {
		obj->last_pos_x = -1;
		return result;
	}
	graph_draw_column(&obj->last_pos_x, true, 155, 235);
	return result;
}

STATIC void graph_simple_column(graph_simple_t *obj, bool only_if_new) {
	if (!graph_enabled()) { obj->last_pos_x = -1; return; }
	/* Use the gap below the round gauge; leave the 41-pixel footer intact. */
	int top = gui_base_window_y0(obj) + gui_base_window_height(obj) + 1;
	if (top < 198) top = 198;
	if (top >= 276) return;
	if (top != obj->top) { obj->top = top; obj->last_pos_x = -1; }
	graph_draw_column(&obj->last_pos_x, only_if_new, top, 277);
}

void MAIN graph_simple_draw(graph_simple_t *obj) {
	((void (*)(void *))graph_simple_draw_original)(obj);
	graph_simple_column(obj, false);
}

void MAIN graph_simple_update(graph_simple_t *obj) {
	((void (*)(void *))graph_simple_update_original)(obj);
	graph_simple_column(obj, true);
}

/* The stock header updater invalidates on every raw MOP or PSP change, even
 * when one-decimal formatting produces the same visible text. Let it refresh
 * its caches and strings with invalidation suppressed, then invalidate only
 * when the rendered strings or widget status actually changed.
 */
void MAIN graph_header_update(void *obj) {
	uint8 *base = (uint8 *)obj;
	uint32 *hwin = (uint32 *)(base + 4);
	uint8 old_left[0x15];
	uint8 old_right[9];
	uint8 old_status[4];

	memcpy_unrolled(old_left, base + 0x90, sizeof(old_left));
	memcpy_unrolled(old_right, base + 0xa5, sizeof(old_right));
	memcpy_unrolled(old_status, base + 0x8c, sizeof(old_status));

	uint32 saved_hwin = *hwin;
	*hwin = 0;
	((graph_header_update_t)graph_header_update_original)(obj);
	*hwin = saved_hwin;

	if (fixed_string_changed(old_left, base + 0x90, sizeof(old_left)) ||
	    fixed_string_changed(old_right, base + 0xa5, sizeof(old_right)) ||
	    bytes_changed(old_status, base + 0x8c, sizeof(old_status)))
		gui_invalidate_window(obj);
}

/* The two pressure values use five-byte display buffers. Filter sub-display
 * raw changes after the stock formatter has updated those buffers.
 */
void MAIN graph_numbers_update(void *obj, int left_var, int right_var) {
	uint8 *base = (uint8 *)obj;
	uint32 *hwin = (uint32 *)(base + 4);
	uint8 old_left[5];
	uint8 old_right[5];

	memcpy_unrolled(old_left, base + 0x10, sizeof(old_left));
	memcpy_unrolled(old_right, base + 0x15, sizeof(old_right));

	uint32 saved_hwin = *hwin;
	*hwin = 0;
	((graph_numbers_update_t)graph_numbers_update_original)(
		obj, left_var, right_var);
	*hwin = saved_hwin;

	if (fixed_string_changed(old_left, base + 0x10, sizeof(old_left)) ||
	    fixed_string_changed(old_right, base + 0x15, sizeof(old_right)))
		gui_method_fc104_setup_and_invalidate(
			saved_hwin, 6, 0x19, 0xd6, 0x2d);
}
