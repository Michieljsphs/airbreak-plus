.syntax unified
.thumb

.section .rodata.graph_abi, "a", %progbits
.align 2

/* 0xffff keeps the graph enabled when no custom setting is assigned. */
.global graph_enable_var_id
.type graph_enable_var_id, %object
graph_enable_var_id:
	.short 0xffff
	.short 0xffff

/* Original stock widget methods replaced by patch_code_graph. */
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

/* Plate bound of the RH estimate (humidity_model.h); keep in step with
 * humid_start_plate_coupling.
 */
.global graph_plate_coupling
.type graph_plate_coupling, %object
graph_plate_coupling:
	.float 0.3

/* Humid Start custom settings shown on the climate chart; 0xffff when not
 * installed (switch: on, Min Tube: no line, coupling: the constant above).
 */
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

/* 1 draws the climate chart instead of the pressure chart
 * (--patch-graph-climate).
 */
.global graph_climate
.type graph_climate, %object
graph_climate:
	.byte 0
