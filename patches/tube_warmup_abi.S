.syntax unified
.thumb

/* Plate bound of the chamber estimate (humidity_model.h): fraction of the
 * plate's rise above ambient over which the chamber adds saturation humidity.
 * Lower values hold the tube back longer. Keep in step with
 * header_climate_plate_coupling.
 */
.section .rodata.tube_warmup_abi, "a", %progbits
.align 2
.global tube_warmup_plate_coupling
.type tube_warmup_plate_coupling, %object
tube_warmup_plate_coupling:
    .float 0.3
.size tube_warmup_plate_coupling, . - tube_warmup_plate_coupling

/* Custom-settings variables for the on/off switch and the factor applied to
 * the estimated added moisture (raw = factor * 100); 0xffff keeps the
 * feature on with a factor of 1.
 */
.global tube_warmup_enable_var_id
.type tube_warmup_enable_var_id, %object
tube_warmup_enable_var_id:
    .short 0xffff
.size tube_warmup_enable_var_id, . - tube_warmup_enable_var_id

.global tube_warmup_factor_var_id
.type tube_warmup_factor_var_id, %object
tube_warmup_factor_var_id:
    .short 0xffff
.size tube_warmup_factor_var_id, . - tube_warmup_factor_var_id
