.syntax unified
.thumb

/* Plate bound of the chamber estimate (humidity_model.h): fraction of the
 * plate's rise above ambient over which the chamber adds saturation humidity.
 * Lower values hold the tube back longer. Keep in step with
 * header_climate_plate_coupling.
 */
.section .rodata.humid_start_abi, "a", %progbits
.align 2
.global humid_start_plate_coupling
.type humid_start_plate_coupling, %object
humid_start_plate_coupling:
    .float 0.3
.size humid_start_plate_coupling, . - humid_start_plate_coupling

/* Custom-settings variable for the on/off switch; 0xffff keeps the feature
 * on.
 */
.global humid_start_enable_var_id
.type humid_start_enable_var_id, %object
humid_start_enable_var_id:
    .short 0xffff
.size humid_start_enable_var_id, . - humid_start_enable_var_id

/* Custom-settings variable for the minimum tube temperature (raw = whole
 * degC); 0xffff uses the lowest tube setting.
 */
.global humid_start_min_tube_var_id
.type humid_start_min_tube_var_id, %object
humid_start_min_tube_var_id:
    .short 0xffff
.size humid_start_min_tube_var_id, . - humid_start_min_tube_var_id

/* Custom-settings variable for the plate coupling (raw = coupling * 100);
 * 0xffff uses humid_start_plate_coupling above.
 */
.global humid_start_coupling_var_id
.type humid_start_coupling_var_id, %object
humid_start_coupling_var_id:
    .short 0xffff
.size humid_start_coupling_var_id, . - humid_start_coupling_var_id
