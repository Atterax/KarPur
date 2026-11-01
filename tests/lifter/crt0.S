// Start of .text: a branch, then the offset of MOD0, as in every Switch executable.
    .section .text.crt0, "ax"
    .global _start
_start:
    b 1f
    .word __mod0 - _start
1:  ret

    .section .rodata.mod0, "a"
    .balign 4
__mod0:
    .ascii "MOD0"
    .word _DYNAMIC - __mod0
    .word __bss_start - __mod0
    .word __bss_end - __mod0
    .word __eh_frame_hdr_start - __mod0
    .word __eh_frame_hdr_end - __mod0
    .word 0
