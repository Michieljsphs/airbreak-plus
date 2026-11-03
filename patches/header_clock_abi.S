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

// Custom-settings Climate switch; 0xffff shows the readout whenever enabled.
.align 1
.global header_climate_var_id
.type header_climate_var_id, %object
header_climate_var_id:
    .short 0xffff
.size header_climate_var_id, . - header_climate_var_id

// Fallback for when the stock humidity model can't run: fraction of the
// plate's rise above ambient used for the chamber outlet's saturation
// temperature; keep in step with tube_warmup_plate_coupling.
.align 2
.global header_climate_plate_coupling
.type header_climate_plate_coupling, %object
header_climate_plate_coupling:
    .float 0.3
.size header_climate_plate_coupling, . - header_climate_plate_coupling

// Custom-settings factor on the stock model's added moisture (raw = factor
// * 100), shared with tube_warmup; 0xffff uses a factor of 1.
.align 1
.global header_climate_factor_var_id
.type header_climate_factor_var_id, %object
header_climate_factor_var_id:
    .short 0xffff
.size header_climate_factor_var_id, . - header_climate_factor_var_id
