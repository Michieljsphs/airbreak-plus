.syntax unified
.thumb

.section .rodata.header_clock_abi, "a", %progbits
.align 2
.global header_clock_var_id
.type header_clock_var_id, %object
header_clock_var_id:
    .short 0xffff
.size header_clock_var_id, . - header_clock_var_id

// Set by --patch-header-climate on SX567 firmware.
.global header_climate_enabled
.type header_climate_enabled, %object
header_climate_enabled:
    .byte 0
.size header_climate_enabled, . - header_climate_enabled

// Fraction of the plate's rise above ambient used for the chamber outlet's
// saturation temperature; keep in step with tube_warmup_plate_coupling.
.align 2
.global header_climate_plate_coupling
.type header_climate_plate_coupling, %object
header_climate_plate_coupling:
    .float 0.3
.size header_climate_plate_coupling, . - header_climate_plate_coupling
