typedef unsigned short UWORD;

volatile UWORD *CUSTOM = (UWORD *)0xdff000;

static UWORD copper[] =
{
    0x0180,0x0000,
    0x0182,0x0fff,
    0xffff,0xfffe
};

void screen_init()
{
    CUSTOM[0x080 >> 1] =
        ((unsigned long)copper) >> 16;

    CUSTOM[0x082 >> 1] =
        ((unsigned long)copper) & 0xffff;
}
