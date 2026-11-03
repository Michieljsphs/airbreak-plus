.syntax unified
.thumb

/* Fraction of the plate's rise above ambient assumed for the chamber outlet's
 * saturation temperature. Lower values hold the tube back longer.
 * Keep in step with header_climate_plate_coupling.
 */
.section .rodata.tube_warmup_abi, "a", %progbits
.align 2
.global tube_warmup_plate_coupling
.type tube_warmup_plate_coupling, %object
tube_warmup_plate_coupling:
    .float 0.3
.size tube_warmup_plate_coupling, . - tube_warmup_plate_coupling

/* Custom-settings variables for the on/off switch and the coupling (raw =
 * coupling * 100); 0xffff keeps the feature on with the constant above.
 */
.global tube_warmup_enable_var_id
.type tube_warmup_enable_var_id, %object
tube_warmup_enable_var_id:
    .short 0xffff
.size tube_warmup_enable_var_id, . - tube_warmup_enable_var_id

.global tube_warmup_coupling_var_id
.type tube_warmup_coupling_var_id, %object
tube_warmup_coupling_var_id:
    .short 0xffff
.size tube_warmup_coupling_var_id, . - tube_warmup_coupling_var_id
