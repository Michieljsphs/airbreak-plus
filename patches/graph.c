/*
 * This replaces the normal pressure gauge code in the ROM.
 *
 * Two charts, chosen when patching (graph_climate):
 * - pressure: a scrolling strip of flow and pressure below the pressure gauge;
 * - climate: the first 60 minutes of therapy, one column per 15 s, with the
 *   plate and tube temperatures and the estimated RH (humidity_model.h), plus
 *   dashed lines for the tube setting, the Humid Start Min Tube and the target
 *   RH. It also takes the space of the mode and pressure readouts (VAuto,
 *   PS 4.0, 7.2, 11.2), which are no longer redrawn while it is shown. The
 *   chart stops after 60 minutes so it is still there in the morning.
 */
#include "stubs.h"
#include "common_code.h"
#include "s10_vars.h"
#include "humidity_model.h"

#define DRAW_PRESSURE 1
#define DRAW_FLOW 0

/* Strip chart area on the screen. The climate chart starts higher, just
 * below the screen title bar, over the mode and pressure readouts.
 */
#define CHART_TOP 155
#define CHART_BOTTOM 235
#define CLIMATE_TOP 74
/* Down to just above the bottom divider, over the humidifier icon. */
#define CLIMATE_BOTTOM 278
#define CLIMATE_HEIGHT (CLIMATE_BOTTOM - CLIMATE_TOP)

#define CLIMATE_COLUMNS 240
/* The therapy timer counts 10 ms ticks: 240 columns of 15 s are 60 minutes. */
#define TICKS_PER_COLUMN 1500
/* A grid tick every 10 minutes. */
#define COLUMNS_PER_TICK 40

typedef struct {
	int last_pos_x;
} graph_state_t;

/* Series, also the scale of a setpoint line. Values are 0 for none,
 * temperatures in half degrees C, RH in percent.
 */
enum { PLATE, TUBE, RH };

/* Only allocated for the climate chart. */
typedef struct {
	graph_state_t base;
	/* Therapy timer at the last update; it going back is a new session. */
	unsigned last_therapy;
	/* Column of the therapy time at the last update; keeps counting after
	 * the 60 minutes.
	 */
	unsigned column;
	uint8 active;
	/* Redraw the whole chart on the next update: the readouts and icon it
	 * covers may have painted over it.
	 */
	uint8 redraw;
	/* Setpoint lines, 0 when not shown. */
	uint8 tube_set;
	uint8 rh_set;
	uint8 min_tube;
	uint8 data[3][CLIMATE_COLUMNS];
} climate_state_t;

typedef int (*graph_draw_t)(int *obj);
typedef int (*graph_update_t)(int *obj, int new_position);
typedef void (*graph_header_update_t)(void *obj);
typedef void (*graph_numbers_update_t)(void *obj, int left_var, int right_var);

extern int variable_get_by_id(int var_id);
extern const unsigned short graph_enable_var_id;
extern const uint32 graph_draw_original;
extern const uint32 graph_update_original;
extern const uint32 graph_header_update_original;
extern const uint32 graph_numbers_update_original;
extern void gui_invalidate_window(void *obj);
extern void gui_method_fc104_setup_and_invalidate(
	uint32 hwin, int left, int top, int right, int bottom);
extern void memcpy_unrolled(void *destination, const void *source,
			    unsigned size);
extern float climate_target_rh_get(void);
extern const uint8 graph_climate;
extern const unsigned short graph_humid_start_var_id;
extern const unsigned short graph_min_tube_var_id;
extern const unsigned short graph_coupling_var_id;
extern const float graph_plate_coupling;

STATIC void init_graph_state(graph_state_t *state) {
	state->last_pos_x = -1;
	if (graph_climate)
		((climate_state_t *)state)->active = ((climate_state_t *)state)->redraw = 0;
}

/* NULL when the heap had no room for it: the graph then draws nothing. */
STATIC graph_state_t *get_graph_state(void) {
	return get_pointer(PTR_GRAPH_DATA,
			   graph_climate ? sizeof(climate_state_t) : sizeof(graph_state_t),
			   (void (*)(void *))&init_graph_state);
}

STATIC void graph_state_reset(void) {
	graph_state_t *state = get_graph_state();
	if (state)
		state->last_pos_x = -1;
}

STATIC bool graph_enabled(void) {
	if (graph_enable_var_id == 0xffffu)
		return true;
	return variable_get_by_id(graph_enable_var_id) != 0;
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

/* The widget's clipping, offset and color, saved while drawing outside it. */
typedef struct {
	short clip[4];
	short x_off, y_off;
	unsigned color;
} gui_saved_t;

// break out of the current clipping so we can draw on the entire screen
STATIC void screen_begin(gui_saved_t *saved) {
	short * const clip = (short*)(gui_context + 8);
	short * const off = (short*)(gui_context + 76);
	for (int i = 0; i < 4; i++)
		saved->clip[i] = clip[i];
	saved->x_off = off[0];
	saved->y_off = off[1];
	saved->color = *(unsigned*)(gui_context + 60);
	clip[0] = 0;
	clip[1] = 0;
	clip[2] = 0x1000;
	clip[3] = 0x1000;
	off[0] = 0;
	off[1] = 0;
}

STATIC void screen_end(const gui_saved_t *saved) {
	short * const clip = (short*)(gui_context + 8);
	short * const off = (short*)(gui_context + 76);
	for (int i = 0; i < 4; i++)
		clip[i] = saved->clip[i];
	off[0] = saved->x_off;
	off[1] = saved->y_off;
	*(unsigned*)(gui_context + 60) = saved->color;
}

/* GUI colors are 0xBBGGRR. */
#define RGB(r, g, b) (((b) << 16) | ((g) << 8) | (r))
#define COLOR_PLATE RGB(0xff, 0x30, 0x30)
#define COLOR_TUBE RGB(0xff, 0x99, 0x00)
#define COLOR_RH RGB(0x40, 0x99, 0xff)
#define COLOR_TUBE_SET RGB(0xb0, 0x6a, 0x00)
#define COLOR_RH_SET RGB(0x2c, 0x6a, 0xb0)
#define COLOR_GRID RGB(0x30, 0x30, 0x30)

/* y of a value: temperatures on a 15-71 degC axis (v in half degrees), RH
 * on a 20-100 % axis, both over the full chart height.
 */
STATIC int value_y(int series, int v) {
	int y = series == RH ? CLIMATE_BOTTOM - (v - 20) * CLIMATE_HEIGHT / 80 :
			       CLIMATE_BOTTOM - (v - 30) * CLIMATE_HEIGHT / 112;
	return clamp(y, CLIMATE_TOP, CLIMATE_BOTTOM);
}

/* Raw degC * 10 to half degrees, 1..255 (0 means no data). */
STATIC int half_degrees(int raw) {
	return clamp((raw + 2) / 5, 1, 255);
}

STATIC float coupling(void) {
	if (graph_coupling_var_id == 0xffffu)
		return graph_plate_coupling;
	return variable_get_by_id(graph_coupling_var_id) * 0.01f;
}

/* Tube setting and Min Tube (0 when not shown), and the target RH. */
STATIC void climate_setpoints(climate_state_t *s) {
	int hts = 0, floor = 0;
	if (variable_get_by_id(VAR_ID_HTB)) {
		hts = half_degrees(variable_get_by_id(VAR_ID_HTS));
		if (graph_min_tube_var_id != 0xffffu &&
		    (graph_humid_start_var_id == 0xffffu ||
		     variable_get_by_id(graph_humid_start_var_id)))
			floor = variable_get_by_id(graph_min_tube_var_id) * 2;
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
STATIC void climate_dash(int series, int v, int x, int pattern) {
	if (v && (x & pattern) == 0) {
		int y = value_y(series, v);
		LCD_FillRect(x, y, x, y);
	}
}

STATIC void climate_draw_column(const climate_state_t *s, int x) {
	static const unsigned colors[3] = { COLOR_PLATE, COLOR_TUBE, COLOR_RH };

	GUI_SetColor(0x000000);
	/* One row above the chart too: the bottom row of the stock readout text
	 * (VAuto, 7.2, 11.2) sits there and shows through otherwise.
	 */
	LCD_FillRect(x, CLIMATE_TOP - 1, x, CLIMATE_BOTTOM);

	// faint grid every 10 degC and a tick every 10 minutes
	GUI_SetColor(COLOR_GRID);
	for (int t = 40; t <= 140; t += 20)
		climate_dash(TUBE, t, x, 3);
	if (x % COLUMNS_PER_TICK == 0)
		LCD_FillRect(x, CLIMATE_BOTTOM - 3, x, CLIMATE_BOTTOM);

	// tube setting and target RH dashed, Min Tube dotted
	GUI_SetColor(COLOR_TUBE_SET);
	climate_dash(TUBE, s->tube_set, x, 4);
	climate_dash(TUBE, s->min_tube, x, 3);
	GUI_SetColor(COLOR_RH_SET);
	climate_dash(RH, s->rh_set, x, 4);

	// plate, then RH, then the tube on top: 2 px lines joined to the
	// previous column
	for (int i = 0; i < 3; i++) {
		int series = i == 0 ? PLATE : i == 1 ? RH : TUBE;
		const uint8 *v = s->data[series];
		if (!v[x])
			continue;
		int y = value_y(series, v[x]);
		int y0 = x > 0 && v[x - 1] ? value_y(series, v[x - 1]) : y;
		GUI_SetColor(colors[series]);
		LCD_FillRect(x, min(y, y0), x, min(max(y, y0) + 1, CLIMATE_BOTTOM));
	}
}

/* Samples a column every 15 s of therapy time for the first 60 minutes,
 * and redraws the whole chart on a new column or after a repaint; a new
 * session clears the chart. Columns are counted from the therapy timer, so
 * the chart starts at the start of therapy even when the therapy screen was
 * not shown then.
 */
STATIC void climate_update(climate_state_t *s) {
	if (*therapy_mode == MODE_OFF) {
		s->active = 0;
		return;
	}
	unsigned therapy = *pap_timer;
	bool all = s->redraw;
	s->redraw = 0;
	if (!s->active || therapy < s->last_therapy) {
		uint8 *p = &s->data[0][0];
		for (unsigned i = 0; i < sizeof(s->data); i++)
			p[i] = 0;
		s->active = 1;
		s->column = ~0u;
		all = true;
	}
	s->last_therapy = therapy;

	/* Setpoints are checked once a column, also after the 60 minutes.
	 * The whole chart is redrawn every column (15 s), so whatever the
	 * stock widgets underneath painted over it (the readouts, the
	 * humidifier icon) is covered again within a column.
	 */
	unsigned column = therapy / TICKS_PER_COLUMN;
	if (column != s->column) {
		s->column = column;
		climate_setpoints(s);
		if (column < CLIMATE_COLUMNS)
			climate_sample(s, column);
		all = true;
	}

	if (all) {
		gui_saved_t saved;
		screen_begin(&saved);
		for (int x = 0; x < CLIMATE_COLUMNS; x++)
			climate_draw_column(s, x);
		screen_end(&saved);
	}
}

/* The climate chart is on screen and covers the mode and pressure readouts. */
STATIC bool climate_covers_readouts(void) {
	return graph_climate && *therapy_mode != MODE_OFF && graph_enabled();
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

STATIC int graph_draw_current_column(bool only_if_new) {
	graph_state_t *state = get_graph_state();
	if (!state)
		return 0;

	// don't do anything if we are not in an active therapy mode
	if (*therapy_mode == 0) {
		state->last_pos_x = -1;
		return 0;
	}

	const unsigned pos_x = (*pap_timer / 15) % 240; // ~6.66px per second (unit of timer is 10ms)
	if (only_if_new && state->last_pos_x == (int)pos_x) return 0;
	state->last_pos_x = pos_x;

	gui_saved_t saved;
	screen_begin(&saved);

	// Draw a strip chart: flow on top, pressure below
	#define HEIGHT_PRES 40
	#define HEIGHT_FLOW 40
	const int top = CHART_TOP;
	const int bottom = CHART_BOTTOM;
	const int p_bottom = top + HEIGHT_FLOW + HEIGHT_PRES;
	const int f_center = top + HEIGHT_FLOW / 2;

	const int command = p_bottom - (int)(p_command * 2.0f);
	const int error = (int)(-p_error * 6.0f);

	GUI_SetColor(0x000000);
	LCD_FillRect(pos_x, top - 1, pos_x + 11, bottom + 1);
	if (breath_progress > 0.0f && breath_progress <= 0.5f) { // Active inhale
		GUI_SetColor(0x101010);
		LCD_FillRect(pos_x, top - 1, pos_x + 1, bottom + 1);
	}

	GUI_SetColor(0x202020);
	for (int y = p_bottom - 40; y < p_bottom; y += 10) // 5, 10, 15, 20 very faintly
		LCD_FillRect(pos_x, y, pos_x + 1, y);
	// draw amplified pressure error with respect to the commanded pressure
	GUI_SetColor(0x000080);
	LCD_FillRect2(pos_x, command, pos_x + 1, command + error);
	// draw the current commanded pressure
	GUI_SetColor(0x00FF88);
	LCD_FillRect(pos_x, command, pos_x + 1, command);

	// Draw the leak-compensated flow variable as solid bars
	GUI_SetColor(0xFF8800);
	LCD_FillRect2(pos_x, f_center, pos_x + 1,
		      f_center - clamp(*flow_compensated / 2, -HEIGHT_FLOW/2, HEIGHT_FLOW/2));
	GUI_SetColor(0x000000);
	LCD_FillRect(pos_x, f_center, pos_x + 1, f_center);

	GUI_SetColor(0x666666);
	for (int y = top; y <= p_bottom; y += HEIGHT_FLOW)
		LCD_FillRect(pos_x, y, pos_x + 3, y);

	screen_end(&saved);

	return 1;
}

// Replaces the stock bar draw method.
int MAIN start(int *obj) {
	if (!graph_enabled()) {
		graph_state_reset();
		return ((graph_draw_t)graph_draw_original)(obj);
	}
	if (graph_climate) {
		// A repaint, also one of only the numeric row the chart covers:
		// redraw the chart on the next update, after the readouts and icon
		// it covers have painted in this repaint.
		climate_state_t *s = (climate_state_t *)get_graph_state();
		if (s)
			s->redraw = 1;
		return 1;
	}
	if (!graph_widget_is_invalidated()) return 0;
	return graph_draw_current_column(false);
}

// The stock update method only redraws when the quantized bar position changes.
// Preserve its state, but drive the time-based graph directly on each new column.
int MAIN graph_widget_update(int *obj, int new_position) {
	if (!graph_enabled()) {
		graph_state_reset();
		return ((graph_update_t)graph_update_original)(obj, new_position);
	}
	obj[2] = new_position;
	if (graph_climate) {
		climate_state_t *s = (climate_state_t *)get_graph_state();
		if (s)
			climate_update(s);
		return 0;
	}
	return graph_draw_current_column(true);
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

	// the climate chart covers the mode and pressure support text
	if (climate_covers_readouts())
		return;
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

	// the climate chart covers the pressure readouts
	if (climate_covers_readouts())
		return;
	if (fixed_string_changed(old_left, base + 0x10, sizeof(old_left)) ||
	    fixed_string_changed(old_right, base + 0x15, sizeof(old_right)))
		gui_method_fc104_setup_and_invalidate(
			saved_hwin, 6, 0x19, 0xd6, 0x2d);
}


// Causes the device to crash and reboot
/*
#if 0
	GUI_SetColor(0x8);
	GUI_FillRect(0, 130, 200, 160);

	GUI_SetColor(0xFF0000);
	GUI_SetFont(font_16); // Causes device to crash and reboot
	//static const char __attribute__((__section__(".text"))) msg[] = "Hello, world!";
	//static const char __attribute__((__section__(".text"))) fmt[] = "%d.%02d";
	GUI_DispStringAt("Hello, world", 10, 130);
	char buf[16];
	int flow = fvars[1] * 100;
	snprintf(buf, sizeof(buf), "%d.%02d", flow / 100, flow % 100);
	GUI_SetColor(0x00FF00);
	GUI_DispStringAt(buf, 40, 150);
#endif

// Also tried:
GUI_SetColor(0xFFFF00);
GUI_SetTextMode(2);
GUI_SetTextAlign(0);
GUI_SetFont_default();
GUI_DispStringAt("Hello, world", 20, top + HEIGHT_FLOW + 20);
// No luck, displays nothing. Changing params of GUI_DispStringAt to short or unsigned short does nothing too.

*/
