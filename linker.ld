SECTIONS {
    . = 0x10000;
    .text : {
        *(.text)
        *(CODE)
    }
    .data : {
        *(.data)
        *(DATA)
    }
    .bss : {
        _bss_start = .;
        *(.bss)
        *(BSS)
        _bss_end = .;
    }
}
