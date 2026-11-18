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
