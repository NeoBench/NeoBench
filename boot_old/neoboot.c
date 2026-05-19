void neoboot_start();

typedef unsigned short UWORD;
typedef unsigned char  UBYTE;

extern volatile UWORD *CUSTOM;

static UWORD cursor_x = 0;
static UWORD cursor_y = 0;

enum
{
    NB_OK,
    NB_WARN,
    NB_FAIL
};

void nb_wait()
{
    volatile int i;

    for(i=0;i<200000;i++)
    {
    }
}

void nb_set_color(UWORD color)
{
    CUSTOM[0x180 >> 1] = color;
}

void nb_print(const char *text)
{
    while(*text)
    {
        (*text++);
    }
}

void nb_status(int type,const char *msg)
{
    switch(type)
    {
        case NB_OK:
            nb_set_color(0x0f00);
            break;

        case NB_WARN:
            nb_set_color(0x0ff0);
            break;

        case NB_FAIL:
            nb_set_color(0x0f00);
            break;
    }

    nb_print(msg);

    nb_wait();
}

void neoboot_banner()
{
    nb_set_color(0x0fff);

    nb_print("====================================\n");
    nb_print("            N E O B O O T          \n");
    nb_print("       NeoBench System Loader      \n");
    nb_print("====================================\n\n");
}

void neoboot_start()
{
    neoboot_banner();

    nb_status(NB_OK,"[  OK  ] CPU detected");
    nb_status(NB_OK,"[  OK  ] MMU enabled");
    nb_status(NB_WARN,"[ WARN ] PPC not detected");
    nb_status(NB_OK,"[  OK  ] Fast RAM online");
    nb_status(NB_OK,"[  OK  ] RTG initialized");
    nb_status(NB_OK,"[  OK  ] NeoCore online");

    nb_print("\nLaunching NeoBench...\n");

    for(;;)
    {
    }
}

