.syntax unified
.thumb

/* Fraction of the plate's rise above ambient assumed for the chamber outlet's
 * saturation temperature. Lower values hold the tube back longer.
 */
.section .rodata.tube_warmup_abi, "a", %progbits
.align 2
.global tube_warmup_plate_coupling
.type tube_warmup_plate_coupling, %object
tube_warmup_plate_coupling:
    .float 0.3
.size tube_warmup_plate_coupling, . - tube_warmup_plate_coupling
