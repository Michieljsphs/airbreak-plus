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

// Set by --patch-header-climate-decimal: temperatures with one decimal.
.global header_climate_decimal
.type header_climate_decimal, %object
header_climate_decimal:
    .byte 0
.size header_climate_decimal, . - header_climate_decimal

// Set by --patch-header-climate-always: the readout replaces every title.
.global header_climate_always
.type header_climate_always, %object
header_climate_always:
    .byte 0
.size header_climate_always, . - header_climate_always

// Custom-settings Climate switch; 0xffff shows the readout whenever enabled.
.align 1
.global header_climate_var_id
.type header_climate_var_id, %object
header_climate_var_id:
    .short 0xffff
.size header_climate_var_id, . - header_climate_var_id

// Plate bound of the chamber estimate (humidity_model.h): fraction of the
// plate's rise above ambient over which the chamber adds saturation humidity;
// keep in step with humid_start_plate_coupling.
.align 2
.global header_climate_plate_coupling
.type header_climate_plate_coupling, %object
header_climate_plate_coupling:
    .float 0.3
.size header_climate_plate_coupling, . - header_climate_plate_coupling

// Custom-settings plate coupling (raw = coupling * 100), shared with
// humid_start; 0xffff uses header_climate_plate_coupling above.
.align 1
.global header_climate_coupling_var_id
.type header_climate_coupling_var_id, %object
header_climate_coupling_var_id:
    .short 0xffff
.size header_climate_coupling_var_id, . - header_climate_coupling_var_id
