typedef unsigned short UWORD;

volatile UWORD *CUSTOM = (UWORD *)0xdff000;

#define COLOR00 0x180

void video_init()
{
    CUSTOM[COLOR00 >> 1] = 0x00f;
}

void video_test()
{
}
