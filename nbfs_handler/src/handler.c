/* 68k AmigaOS filesystem handler entry: startup, device open, service loop.
 * Build with -DNBFH_AMIGA for the 68k target (api68k.h is included via
 * dospackets.h? no -- included here directly). */

#include "dospackets.h"
#include "api68k.h"
#include <string.h>

struct nbh_startup_s
{
    uint32_t fs_unit;      /* 0  dos/exec unit number         */
    uint32_t fs_devname;   /* 4  BPTR to BSTR device name     */
    uint32_t fs_env;       /* 8  BPTR environment             */
    uint32_t fs_flags;     /* 12 flags                        */
};

struct nbh_exctx
{
    void *sysbase;
    void *io_port;
    struct nbfh_ioreq *io;
    struct nbh_vol vol;
    struct nbh_rt rt;
    char devname[64];
    uint32_t vol_start;   /* partition start in device sectors (LBA) */
};

static struct nbh_exctx g_x;

static int h_read(void *ud, uint32_t sector, uint32_t count, void *buf)
{
    struct nbh_exctx *x = (struct nbh_exctx *)ud;
    struct nbfh_ioreq *io = x->io;

    io->command = EXEC_CMD_READ;
    io->flags = 0;
    io->error = 0;
    io->data = buf;
    /* The NBFS volume sits at the partition start within the device, so
     * translate volume-sector -> device-sector and then to a byte offset.
     * NBFS block = 8 * 512-byte device sectors. */
    io->offset = (sector + x->vol_start) * NBH_SECTOR_SIZE;
    io->len = count * NBH_SECTOR_SIZE;
    io->actual = 0;

    /* waitport not needed: DoIO blocks the task. */
    nbh_doio(x->sysbase, io);
    return io->error == 0 && io->actual == io->len ? 0 : -1;
}

static void *h_alloc(void *ud, uint32_t size)
{
    struct nbh_exctx *x = (struct nbh_exctx *)ud;
    return nbh_alloc(x->sysbase, size, EXECF_MEMF_PUBLIC | EXECF_MEMF_CLEAR);
}

static void h_free(void *ud, void *p, uint32_t size)
{
    struct nbh_exctx *x = (struct nbh_exctx *)ud;
    nbh_free(x->sysbase, p, size);
}

static void h_reply(void *ud, struct nbfh_pkt *pkt)
{
    struct nbh_exctx *x = (struct nbh_exctx *)ud;
    nbh_replymsg(x->sysbase, (void *)(uintptr_t)pkt->link);
}

static uint32_t envec_ld(const uint8_t *p, uint32_t ofs)
{
    /* DosEnvec is big-endian IEEE longs; m68k matches, plain deref. */
    uint32_t v;
    __builtin_memcpy(&v, p + ofs, 4);
    return v;
}

static uint32_t startup_open(struct nbfh_pkt *pkt)
{
    struct nbh_startup_s *ss;
    const uint8_t *bstr;
    uint32_t len;

    ss = (struct nbh_startup_s *)NBH_BDEC(pkt->arg[0]);
    if (!ss)
        return ERROR_OBJECT_NOT_FOUND;

    bstr = (const uint8_t *)(uintptr_t)(ss->fs_devname << 2);
    if (!bstr)
        return ERROR_OBJECT_NOT_FOUND;

    len = bstr[0];
    if (len > sizeof(g_x.devname) - 1)
        len = sizeof(g_x.devname) - 1;
    memcpy(g_x.devname, bstr + 1, len);
    g_x.devname[len] = 0;

    /* Derive partition start (device sectors) from the DosEnvec so the
     * NBFS volume offset is added to every raw device read. */
    g_x.vol_start = 0;
    if (ss->fs_env)
    {
        const uint8_t *e = (const uint8_t *)(uintptr_t)(ss->fs_env << 2);
        uint32_t surfaces = envec_ld(e, 3 * 4);
        uint32_t bpt = envec_ld(e, 5 * 4);
        uint32_t lowcyl = envec_ld(e, 9 * 4);
        g_x.vol_start = lowcyl * surfaces * bpt;
    }

    if (nbh_open_device(g_x.sysbase, g_x.devname, ss->fs_unit, g_x.io, 0))
        return ERROR_DEVICE_NOT_MOUNTED;

    if (nbh_mount(&g_x.vol) != 0)
    {
        nbh_close_device(g_x.sysbase, g_x.io);
        return ERROR_NOT_A_DOS_DISK;
    }

    return 0;
}

void handler_main(void)
{
    struct nbfh_pkt *pkt;

    memset(&g_x, 0, sizeof(g_x));
    g_x.sysbase = (void *)(uintptr_t)*(volatile uint32_t *)4;

    g_x.rt.ud = &g_x;
    g_x.rt.alloc = h_alloc;
    g_x.rt.free = h_free;
    g_x.rt.reply = h_reply;

    g_x.vol.io.ud = &g_x;
    g_x.vol.io.read = h_read;

    g_x.io_port = nbh_create_msgport(g_x.sysbase);
    if (!g_x.io_port)
        return;
    g_x.io = (struct nbfh_ioreq *)nbh_create_iorequest(g_x.sysbase,
                                                        g_x.io_port,
                                                        sizeof(*g_x.io));
    if (!g_x.io)
    {
        nbh_delete_msgport(g_x.sysbase, g_x.io_port);
        return;
    }

    nbh_waitport(g_x.sysbase, g_x.io_port);
    pkt = nbh_getmsg(g_x.sysbase, g_x.io_port);
    if (pkt && pkt->type == ACTION_STARTUP)
    {
        uint32_t err = startup_open(pkt);
        pkt->res1 = err;
        pkt->res2 = 0;
        h_reply(&g_x, pkt);
    }

    for (;;)
    {
        struct nbfh_pkt *sp;

        nbh_waitport(g_x.sysbase, g_x.io_port);
        while ((sp = (struct nbfh_pkt *)nbh_getmsg(g_x.sysbase,
                                                    g_x.io_port)) != 0)
        {
            switch (sp->type)
            {
                case ACTION_STARTUP:
                    sp->res1 = startup_open(sp);
                    sp->res2 = 0;
                    break;
                case ACTION_DIE:
                    sp->res1 = DOSTRUE;
                    sp->res2 = 0;
                    h_reply(&g_x, sp);
                    nbh_delete_iorequest(g_x.sysbase, g_x.io);
                    nbh_delete_msgport(g_x.sysbase, g_x.io_port);
                    return;
                case ACTION_INHIBIT:
                    sp->res1 = 0;
                    sp->res2 = 0;
                    break;
                default:
                    g_x.rt.handler_port = sp->port;
                    nbh_serve(&g_x.vol, &g_x.rt, sp);
                    continue;
            }
            h_reply(&g_x, sp);
        }
    }
}