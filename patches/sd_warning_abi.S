.syntax unified
.thumb

/* Patched with the displaced stock card-check call, the stock
 * set-bitfield-member function, the card-status object's stock vtable and
 * ZPK's g[6] index.
 */
.section .rodata.sd_warning_abi, "a", %progbits
.align 2
.global sd_warning_original
.type sd_warning_original, %object
sd_warning_original:
    .word 0xffffffff
.size sd_warning_original, . - sd_warning_original
.global sd_warning_member_set
.type sd_warning_member_set, %object
sd_warning_member_set:
    .word 0xffffffff
.size sd_warning_member_set, . - sd_warning_member_set
.global sd_warning_vtable
.type sd_warning_vtable, %object
sd_warning_vtable:
    .word 0xffffffff
.size sd_warning_vtable, . - sd_warning_vtable
.global sd_warning_zpk_index
.type sd_warning_zpk_index, %object
sd_warning_zpk_index:
    .short 0xffff
.size sd_warning_zpk_index, . - sd_warning_zpk_index
