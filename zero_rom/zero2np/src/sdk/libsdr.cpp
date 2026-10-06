/* ==========================================================================
 *  libsdr.cpp  (remote SPU2 library -- EE half -- PC-port shim)
 *
 *  See libsdr.h.  The RPC becomes a direct call into the libsd shim, because
 *  on the host both CPUs are one process.
 * ======================================================================== */

#include "libsdr.h"

#include "libsd.h"

#include <stdarg.h>

extern "C" {

int sceSdRemoteInit(void)
{
    return 0;
}

int sceSdRemote(int wait, u_int fno, ...)
{
    va_list ap;
    int     ret = 0;

    /* `wait` 0 would post the call and let it complete on its own thread.
     * Nothing in the ROM does that, and running it inline is a superset of
     * that behaviour anyway, so the flag is only noted. */
    (void)wait;

    va_start(ap, fno);

    switch (fno)
    {
    case SDR_SET_PARAM:
    {
        int entry = va_arg(ap, int);
        int value = va_arg(ap, int);

        sceSdSetParam((u_short)entry, (u_short)value);
        break;
    }

    case SDR_VOICE_TRANS:
    {
        int    chan    = va_arg(ap, int);
        int    mode    = va_arg(ap, int);
        u_int  iopaddr = (u_int)va_arg(ap, int);
        int    spuaddr = va_arg(ap, int);
        int    size    = va_arg(ap, int);

        /* The source is an IOP address the ROM carried through an int -- see
         * sceSifSetDma() in sif.cpp, which is what put the bytes there. */
        ret = sceSdVoiceTrans((short)chan, (u_int)mode,
                              (u_char *)(uintptr_t)iopaddr,
                              (u_int)spuaddr, (u_int)size);
        break;
    }

    case SDR_BLOCK_TRANS:
    {
        int   chan    = va_arg(ap, int);
        int   mode    = va_arg(ap, int);
        u_int iopaddr = (u_int)va_arg(ap, int);
        int   size    = va_arg(ap, int);
        int   start   = 0;

        /* PORT: audioDecPause() stops the auto-DMA with four arguments and
         * audioDecResume() restarts it with five.  On the EE the fifth
         * argument register was simply whatever the caller left there, and
         * sceSdBlockTrans() ignores start_addr unless the mode is CONT; here
         * reading a va_arg the caller never pushed is undefined, so the
         * argument is only fetched when the mode says it exists. */
        if (mode == SD_BLOCK_TRANS_CONT)
        {
            start = va_arg(ap, int);
        }

        ret = sceSdBlockTrans((short)chan, (u_short)mode,
                              (u_char *)(uintptr_t)iopaddr,
                              (u_int)size, (u_int)start);
        break;
    }

    case SDR_BLOCK_TRANS_STATUS:
    {
        /* PORT: same shape.  audioDecSendToIOP() passes the core and nothing
         * else, so `flag` is the 0 that means "report the play address". */
        int chan = va_arg(ap, int);

        ret = sceSdBlockTransStatus((short)chan, 0);
        break;
    }

    default:
        /* Only the four audiodec.c uses are wired up.  Anything else is a new
         * call site rather than a fault, so name it once and answer 0. */
        {
            static int reported;

            if (reported < 8)
            {
                reported++;
                printf("sceSdRemote: unhandled function %#x\n", fno);
            }
        }
        break;
    }

    va_end(ap);

    return ret;
}

}
