/*
 * Preference loading.
 *
 * Four plain text files, Config/screen.cfg, Config/pointer.cfg,
 * Config/sound.cfg and Config/boot.cfg, come out of the ROM's file store
 * and set the shape
 * of the desktop before it is composited.  The format is deliberately
 * the dullest thing that works
 * -- "key = value", '#' starts a comment -- because the point of these
 * files is that they can be edited without a tool.
 *
 * Parsing never fails: an unknown key or a value that will not convert
 * leaves the default in place.  A broken file therefore costs its own
 * effect and nothing else, which is the only reasonable behaviour for
 * configuration read during boot.
 */
#include "prefs.h"
#include "pfs.h"
#include "amiga.h"
#include "gfx.h"

struct nb_prefs nb_prefs;

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static const char *skip_space(const char *p, const char *end)
{
    while (p < end && is_space(*p))
        p++;
    return p;
}

static int key_is(const char *k, unsigned len, const char *name)
{
    unsigned i;

    for (i = 0; name[i]; i++)
        if (i >= len || k[i] != name[i])
            return 0;
    return i == len;
}

static unsigned to_u(const char *v, unsigned def)
{
    unsigned n = 0;
    int seen = 0;

    while (*v == ' ' || *v == '\t')
        v++;
    while (*v >= '0' && *v <= '9')
    {
        n = n * 10u + (unsigned)(*v - '0');
        v++;
        seen = 1;
    }
    return seen ? n : def;
}

static int on_off(const char *v, int def)
{
    char c = v[0];

    if (c >= 'A' && c <= 'Z')
        c = (char)(c - 'A' + 'a');

    switch (c)
    {
    case 'y': case 't': case '1':            /* yes, true, 1              */
        return 1;
    case 'n': case 'f': case '0':            /* no, false, 0              */
        return 0;
    case 'o':                                /* on, off                   */
        return (v[1] == 'n' || v[1] == 'N') ? 1 : 0;
    default:
        return def;
    }
}

static int hexv(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* "#RRGGBB" (or without the hash) into RGB565, 0 if it is not a colour. */
static uint16_t to_rgb(const char *v, uint16_t def)
{
    int h0, h1, h2, h3, h4, h5;

    while (*v == ' ')
        v++;
    if (*v == '#')
        v++;
    h0 = hexv(v[0]); h1 = hexv(v[1]);
    h2 = hexv(v[2]); h3 = hexv(v[3]);
    h4 = hexv(v[4]); h5 = hexv(v[5]);
    if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0 || h5 < 0)
        return def;
    return (uint16_t)((((uint16_t)(h0 * 16 + h1) >> 3) << 11) |
                      (((uint16_t)(h2 * 16 + h3) >> 2) << 5) |
                       ((uint16_t)(h4 * 16 + h5) >> 3));
}

static void to_shape(const char *v)
{
    if (v[0] == 'c' || v[0] == 'C')
        nb_prefs.shape = NB_PTR_CROSS;
    else if (v[0] == 'i' || v[0] == 'I')
        nb_prefs.shape = NB_PTR_IBEAM;
    else if (v[0] == 'd' || v[0] == 'D')
        nb_prefs.shape = NB_PTR_DOT;
    else
        nb_prefs.shape = NB_PTR_ARROW;
}

/* "x,y" for the pointer's parking spot */
static void to_pair(const char *v, int *a, int *b)
{
    unsigned x, y;
    const char *comma = v;

    while (*comma && *comma != ',')
        comma++;
    if (*comma != ',')
        return;
    x = to_u(v, (unsigned)*a);
    y = to_u(comma + 1, (unsigned)*b);
    *a = (int)x;
    *b = (int)y;
}

/* strcpy without the library */
static void copy_str(char *dst, unsigned cap, const char *src)
{
    unsigned i;

    for (i = 0; i + 1u < cap && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* A preference value compared without regard to case, so "Xen" and
 * "xen" name the same face. */
static int fold(char c)
{
    return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

static int name_is(const char *v, const char *want)
{
    unsigned i;

    for (i = 0; want[i]; i++)
        if (!v[i] || fold(v[i]) != fold(want[i]))
            return 0;
    return !v[i];
}

/* ------------------------------------------------------------------ *
 * Backdrops
 * ------------------------------------------------------------------ */

static const char *const bd_name[NB_BD_COUNT] = {
    "wash", "paper", "azure", "dusk", "slate"
};

const char *nb_bd_name(int i)
{
    if (i < 0 || i >= NB_BD_COUNT)
        i = NB_BD_WASH;
    return bd_name[i];
}

int nb_bd_index(const char *v)
{
    int i;

    for (i = 0; i < NB_BD_COUNT; i++)
        if (name_is(v, bd_name[i]))
            return i;
    return -1;
}

/*
 * The pair of colours the gradient is painted with.  Wash takes the two
 * Config/screen.cfg carries, so a file that sets them and says nothing
 * about the backdrop has exactly the desktop it had before there were
 * backdrops to choose from -- and the four fixed pairs are all light
 * fields, because the mark, the wordmark, the horizon glows and the
 * Workbench chrome over them were drawn against one.  They differ by
 * hue rather than by weight: a cream, a sky, a peach falling to lilac
 * and a cool grey-blue, against wash's white and mint.
 */
void nb_bd_colours(int i, uint16_t *top, uint16_t *bot)
{
    if (i < 0 || i >= NB_BD_COUNT)
        i = NB_BD_WASH;

    if (i == NB_BD_WASH)
    {
        *top = nb_prefs.bg_top;
        *bot = nb_prefs.bg_bot;
        return;
    }

    switch (i)
    {
    case NB_BD_PAPER:                       /* warm cream                 */
        *top = NB_RGB(31, 62, 30);          /* #FDFBF4                   */
        *bot = NB_RGB(29, 57, 26);          /* #EDE5D3                   */
        break;
    case NB_BD_AZURE:                       /* pale sky                   */
        *top = NB_RGB(29, 61, 31);          /* #E8F4FF                   */
        *bot = NB_RGB(23, 54, 30);          /* #BBD9F2                   */
        break;
    case NB_BD_DUSK:                        /* peach falling to lilac     */
        *top = NB_RGB(31, 57, 26);          /* #FCE7D6                   */
        *bot = NB_RGB(27, 52, 30);          /* #DDD2F0                   */
        break;
    default:                                /* cool slate grey            */
        *top = NB_RGB(28, 58, 29);          /* #E3E8EF                   */
        *bot = NB_RGB(23, 48, 25);          /* #B9C2CE                   */
        break;
    }
}

static void set_key(const char *k, unsigned klen, const char *v)
{
    if (key_is(k, klen, "width"))          nb_prefs.w       = to_u(v, 640);
    else if (key_is(k, klen, "height"))    nb_prefs.h       = to_u(v, 512);
    else if (key_is(k, klen, "depth"))     nb_prefs.depth   = to_u(v, 8);
    else if (key_is(k, klen, "mode"))
    {
        /* "hires-lace" carries the interlace flag in the name */
        const char *s = v;

        nb_prefs.lace = 0;
        while (s[0] && s[1])
        {
            if ((s[0] == 'l' || s[0] == 'L') &&
                (s[1] == 'a' || s[1] == 'A'))
            {
                nb_prefs.lace = 1;
                break;
            }
            s++;
        }
    }
    else if (key_is(k, klen, "grid"))      nb_prefs.grid    = on_off(v, 1);
    else if (key_is(k, klen, "glow"))      nb_prefs.glow    = on_off(v, 1);
    else if (key_is(k, klen, "taskbar"))   nb_prefs.taskbar = on_off(v, 1);
    else if (key_is(k, klen, "bar"))
    {
        /* "aero" is the default and anything else is the flat field the
         * desktop was drawn with before the glass existed */
        const char *s = v;

        nb_prefs.bar_style = BAR_AERO;
        while (*s)
        {
            if (*s == 'c' || *s == 'C')
            {
                nb_prefs.bar_style = BAR_CLASSIC;
                break;
            }
            s++;
        }
    }
    else if (key_is(k, klen, "glass"))
    {
        /* how much of the backdrop the bar lets through: 0 is a painted
         * field, 100 is nearly not there at all */
        unsigned u = to_u(v, 20);
        nb_prefs.bar_glass = (u > 100u) ? 100u : u;
    }
    else if (key_is(k, klen, "bg_top"))    nb_prefs.bg_top  = to_rgb(v, nb_prefs.bg_top);
    else if (key_is(k, klen, "bg_bot"))    nb_prefs.bg_bot  = to_rgb(v, nb_prefs.bg_bot);
    else if (key_is(k, klen, "backdrop"))
    {
        /* named, so a file can be read months later without knowing
         * what number the pane has since put the choice at */
        int i = nb_bd_index(v);

        if (i >= 0)
            nb_prefs.backdrop = i;
    }
    else if (key_is(k, klen, "font"))
        copy_str(nb_prefs.font, (unsigned)sizeof(nb_prefs.font), v);
    else if (key_is(k, klen, "shape"))     to_shape(v);
    else if (key_is(k, klen, "scale"))
    {
        unsigned s = to_u(v, 1);
        nb_prefs.scale = (s >= 2) ? 2 : 1;
    }
    else if (key_is(k, klen, "speed"))
    {
        unsigned s = to_u(v, 2);
        nb_prefs.speed = (int)((s < 1) ? 1 : (s > 8 ? 8 : s));
    }
    else if (key_is(k, klen, "shadow"))    nb_prefs.shadow  = on_off(v, 1);
    else if (key_is(k, klen, "visible"))   nb_prefs.visible = on_off(v, 1);
    else if (key_is(k, klen, "colour") || key_is(k, klen, "color"))
        nb_prefs.colour = to_rgb(v, nb_prefs.colour);
    else if (key_is(k, klen, "start"))
        to_pair(v, &nb_prefs.start_x, &nb_prefs.start_y);
    else if (key_is(k, klen, "startup"))
        nb_prefs.snd_startup = on_off(v, 1);
    else if (key_is(k, klen, "volume"))
    {
        unsigned u = to_u(v, 48);
        nb_prefs.snd_volume = (u > 64u) ? 64u : u;
    }
    else if (key_is(k, klen, "file"))
    {
        /* the path is a plain string, trimmed already by the caller */
        copy_str(nb_prefs.snd_file, (unsigned)sizeof(nb_prefs.snd_file), v);
    }
    else if (key_is(k, klen, "hold"))
    {
        /* seconds, and bounded: a hold that runs away would hold the
         * boot rather than the screen, and 15 s is already a long wait */
        unsigned u = to_u(v, 3);
        nb_prefs.hold = (u > 15u) ? 15u : u;
    }
    else if (key_is(k, klen, "scan"))
        nb_prefs.hwscan = on_off(v, 1);
}

static void apply(const struct pfs_node *n)
{
    const char *p   = (const char *)n->data;
    const char *end = p + n->size;

    while (p < end)
    {
        const char *eol = p;
        const char *eq;
        const char *k, *kend, *v, *vend;

        while (eol < end && *eol != '\n')
            eol++;

        k = skip_space(p, eol);
        if (k < eol && *k != '#')
        {
            eq = k;
            while (eq < eol && *eq != '=')
                eq++;
            if (eq < eol)
            {
                kend = eq;
                while (kend > k && is_space(kend[-1]))
                    kend--;
                v = skip_space(eq + 1, eol);
                vend = eol;
                while (vend > v && is_space(vend[-1]))
                    vend--;
                if (vend > v)
                {
                    /* the parser works on NUL terminated values */
                    char buf[40];
                    unsigned len = (unsigned)(vend - v);
                    unsigned i;

                    if (len > sizeof(buf) - 1u)
                        len = sizeof(buf) - 1u;
                    for (i = 0; i < len; i++)
                        buf[i] = v[i];
                    buf[len] = '\0';
                    set_key(k, (unsigned)(kend - k), buf);
                }
            }
        }
        p = (eol < end) ? eol + 1 : end;
    }
}

static void defaults(void)
{
    nb_prefs.w       = 640;
    nb_prefs.h       = 512;
    nb_prefs.depth   = 8;
    nb_prefs.lace    = 1;
    nb_prefs.grid    = 1;
    nb_prefs.glow    = 1;
    nb_prefs.taskbar = 1;
    /* the NeoBench backdrop the desktop draws when there is no file to
     * read: white over mint */
    nb_prefs.bg_top  = NB_RGB(31, 63, 31);
    nb_prefs.bg_bot  = NB_RGB(27, 59, 28);
    nb_prefs.backdrop = NB_BD_WASH;

    nb_prefs.shape   = NB_PTR_ARROW;
    nb_prefs.scale   = 1;
    nb_prefs.speed   = 2;
    nb_prefs.shadow  = 1;
    nb_prefs.visible = 1;
    nb_prefs.colour  = NB_RGB(31, 63, 31);
    nb_prefs.start_x = 320;
    nb_prefs.start_y = 256;

    nb_prefs.snd_startup = 1;
    nb_prefs.snd_volume   = 48;
    /* Xen at nine pixels is the standard face */
    copy_str(nb_prefs.font, (unsigned)sizeof(nb_prefs.font), "Xen");
    copy_str(nb_prefs.snd_file, (unsigned)sizeof(nb_prefs.snd_file),
             "Core/Media/startup.snd");

    nb_prefs.hold      = 3;
    nb_prefs.hwscan    = 1;
    nb_prefs.bar_style = BAR_AERO;
    nb_prefs.bar_glass = 20;
}

void nb_prefs_load(void)
{
    const struct pfs_node *n;

    defaults();

    n = pfs_find("Config/screen.cfg");
    if (n && !n->dir)
        apply(n);
    n = pfs_find("Config/pointer.cfg");
    if (n && !n->dir)
        apply(n);
    n = pfs_find("Config/sound.cfg");
    if (n && !n->dir)
        apply(n);
    n = pfs_find("Config/boot.cfg");
    if (n && !n->dir)
        apply(n);

    /*
     * The face is selected here rather than in the desktop, so the boot
     * console, the panel and every program window are all setting type
     * in one face from the first frame after the files are read.
     */
    gfx_font(name_is(nb_prefs.font, "system") ||
             name_is(nb_prefs.font, "8x8")    ||
             name_is(nb_prefs.font, "console")
             ? NB_FONT_SYS : NB_FONT_XEN);
}

/* ------------------------------------------------------------------ *
 * Serial read-out
 * ------------------------------------------------------------------ */

static void put(const char *s)
{
    while (*s)
        amiga_serial_putc(*s++);
}

static void put_u(unsigned v)
{
    char buf[12];
    int i = (int)sizeof(buf);

    buf[--i] = '\0';
    do
    {
        buf[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v);
    put(&buf[i]);
}

static void put_x(uint16_t v)
{
    static const char hex[] = "0123456789abcdef";
    char buf[5];

    buf[0] = hex[(v >> 12) & 15];
    buf[1] = hex[(v >> 8) & 15];
    buf[2] = hex[(v >> 4) & 15];
    buf[3] = hex[v & 15];
    buf[4] = '\0';
    put(buf);
}

static const char *shape_name(void)
{
    switch (nb_prefs.shape)
    {
    case NB_PTR_CROSS: return "cross";
    case NB_PTR_IBEAM: return "ibeam";
    case NB_PTR_DOT:   return "dot";
    default:           return "arrow";
    }
}

void nb_prefs_dump(void)
{
    put(">prefs screen ");
    put_u(nb_prefs.w);
    put("x");
    put_u(nb_prefs.h);
    put("x");
    put_u(nb_prefs.depth);
    put(nb_prefs.lace ? " lace" : " p");
    put(" bg=#");
    put_x(nb_prefs.bg_top);
    put("/#");
    put_x(nb_prefs.bg_bot);
    put(nb_prefs.grid ? " grid=on" : " grid=off");
    put(nb_prefs.glow ? " glow=on" : " glow=off");
    put(nb_prefs.taskbar ? " taskbar=on" : " taskbar=off");
    put(nb_prefs.bar_style == BAR_CLASSIC ? " bar=classic" : " bar=aero");
    put(" glass=");
    put_u(nb_prefs.bar_glass);
    put(" backdrop=");
    put(nb_bd_name(nb_prefs.backdrop));
    put(" font=");
    put(nb_prefs.font);
    put("\n\r");

    put(">prefs pointer shape=");
    put(shape_name());
    put(" scale=");
    put_u((unsigned)nb_prefs.scale);
    put(" speed=");
    put_u((unsigned)nb_prefs.speed);
    put(" colour=#");
    put_x(nb_prefs.colour);
    put(nb_prefs.shadow ? " shadow=on" : " shadow=off");
    put(nb_prefs.visible ? " visible=on" : " visible=off");
    put(" start=");
    put_u((unsigned)nb_prefs.start_x);
    put(",");
    put_u((unsigned)nb_prefs.start_y);
    put("\n\r");

    put(">sound startup=");
    put(nb_prefs.snd_startup ? "on" : "off");
    put(" volume=");
    put_u(nb_prefs.snd_volume);
    put(" file=");
    put(nb_prefs.snd_file);
    put("\n\r");

    put(">prefs boot hold=");
    put_u(nb_prefs.hold);
    put(nb_prefs.hwscan ? " scan=on" : " scan=off");
    put("\n\r");
}
