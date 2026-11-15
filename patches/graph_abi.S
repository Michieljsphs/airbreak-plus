.syntax unified
.thumb

.section .rodata.graph_abi, "a", %progbits
.align 2

/* Original widget methods called by the therapy-screen graph wrappers. */
.global graph_draw_original
.type graph_draw_original, %object
graph_draw_original:
	.word 0xffffffff

.global graph_update_original
.type graph_update_original, %object
graph_update_original:
	.word 0xffffffff

.global graph_header_update_original
.type graph_header_update_original, %object
graph_header_update_original:
	.word 0xffffffff

.global graph_numbers_update_original
.type graph_numbers_update_original, %object
graph_numbers_update_original:
	.word 0xffffffff

.global graph_simple_draw_original
graph_simple_draw_original:
	.word 0xffffffff
.global graph_simple_update_original
graph_simple_update_original:
	.word 0xffffffff

/* Climate chart (--patch-graph-climate): 1 draws it instead of the trace. */
.global graph_climate
.type graph_climate, %object
graph_climate:
	.byte 0

/* Chart width in minutes (--graph-width); the default is 60. */
.balign 4
.global graph_width_minutes
.type graph_width_minutes, %object
graph_width_minutes:
	.word 60

/* Plate bound of the RH estimate (humidity_model.h); keep in step with
 * humid_start_plate_coupling.
 */
.global graph_plate_coupling
.type graph_plate_coupling, %object
graph_plate_coupling:
	.float 0.3

/* Custom settings shown on the climate chart; 0xffff when not installed:
 * Graph Width (raw = tenths of an hour; none: graph_width_minutes), Humid
 * Start (switch: on, Min Tube: no line, coupling: the constant above).
 */
.balign 2
.global graph_width_var_id
.type graph_width_var_id, %object
graph_width_var_id:
	.short 0xffff

.global graph_humid_start_var_id
.type graph_humid_start_var_id, %object
graph_humid_start_var_id:
	.short 0xffff

.global graph_min_tube_var_id
.type graph_min_tube_var_id, %object
graph_min_tube_var_id:
	.short 0xffff

.global graph_coupling_var_id
.type graph_coupling_var_id, %object
graph_coupling_var_id:
	.short 0xffff
