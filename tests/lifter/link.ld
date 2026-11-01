/* Lay the test program out like an NSO: .text, then read-only data, then data and bss, each page-aligned. */
SECTIONS {
    . = 0;
    .text : { *(.text.crt0) *(.text .text.*) }
    HIDDEN(__text_end = .);
    . = ALIGN(0x1000);
    HIDDEN(__ro_start = .);
    .rodata : { *(.rodata.mod0) *(.rodata .rodata.*) }
    .eh_frame_hdr : { HIDDEN(__eh_frame_hdr_start = .); *(.eh_frame_hdr) HIDDEN(__eh_frame_hdr_end = .); }
    .eh_frame : { *(.eh_frame) }
    .dynsym : { *(.dynsym) }
    .dynstr : { *(.dynstr) }
    .hash : { *(.hash) }
    .gnu.hash : { *(.gnu.hash) }
    .rela.dyn : { *(.rela.dyn) }
    HIDDEN(__ro_end = .);
    . = ALIGN(0x1000);
    HIDDEN(__data_start = .);
    .data.rel.ro : { *(.data.rel.ro .data.rel.ro.*) }
    .dynamic : { *(.dynamic) }
    .got : { *(.got) *(.got.plt) }
    .data : { *(.data .data.*) }
    HIDDEN(__data_end = .);
    HIDDEN(__bss_start = .);
    .bss : { *(.bss .bss.*) *(COMMON) }
    HIDDEN(__bss_end = .);
    /DISCARD/ : { *(.comment) *(.note*) }
}
