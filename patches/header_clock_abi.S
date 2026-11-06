.syntax unified
.thumb

.section .rodata.header_clock_abi, "a", %progbits
.align 2
.global header_clock_var_id
.type header_clock_var_id, %object
header_clock_var_id:
    .short 0xffff
.size header_clock_var_id, . - header_clock_var_id
