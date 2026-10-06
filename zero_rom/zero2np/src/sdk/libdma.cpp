/* ==========================================================================
 *  libdma.cpp  (SCE DMA library -- PC-port shim)
 * ======================================================================== */

#include "libdma.h"

#include <string.h>

static sceDmaEnv s_dmaEnv;
static sceDmaChan s_dmaChannels[16];

extern "C" {

int sceDmaReset(int mode)
{
    (void)mode;
    memset(&s_dmaEnv, 0, sizeof(s_dmaEnv));
    memset(s_dmaChannels, 0, sizeof(s_dmaChannels));
    return 0;
}

sceDmaEnv *sceDmaGetEnv(sceDmaEnv *env)
{
    if (env != 0)
    {
        *env = s_dmaEnv;
    }
    return env;
}

int sceDmaPutEnv(sceDmaEnv *env)
{
    if (env != 0)
    {
        s_dmaEnv = *env;
    }
    return 0;
}

sceDmaChan *sceDmaGetChan(int id)
{
    if (id < 0 || id >= (int)(sizeof(s_dmaChannels) / sizeof(s_dmaChannels[0])))
    {
        return 0;
    }
    return &s_dmaChannels[id];
}

void sceDmaSend(sceDmaChan *d, void *tag)
{
    if (d != 0)
    {
        d->tadr = (sceDmaTag *)tag;
        d->chcr.STR = 0;
    }
}

int sceDmaSync(sceDmaChan *d, int mode, int timeout)
{
    (void)d;
    (void)mode;
    (void)timeout;
    return 0;
}

}
