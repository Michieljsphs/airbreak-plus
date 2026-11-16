.syntax unified
.thumb
.section .rodata
.balign 4

/* Hold time from which another feature consumes the Home hold at its
 * threshold; the humidifier leaves such holds alone. 0xffffffff: none.
 */
.global humidifier_hold_max_ms
.type humidifier_hold_max_ms,%object
humidifier_hold_max_ms:
    .word 0xffffffff
.size humidifier_hold_max_ms,4
