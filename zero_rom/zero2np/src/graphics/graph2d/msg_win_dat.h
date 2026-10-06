/* ==========================================================================
 *  graphics/graph2d/msg_win_dat.h
 *
 *  Declares the default message-window rect table msg_win_data[] (defined in
 *  msg_win_dat.c): the per-win-id window rectangle the message system uses when
 *  a message type does not override its window.
 *
 *  msg_win_dat.c (the initialised table) is not reconstructed yet; this header
 *  lets consumers link against a real declaration instead of an inline extern.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_GRAPH2D_MSG_WIN_DAT_H
#define _GRAPHICS_GRAPH2D_MSG_WIN_DAT_H

#include "message.h"                /* MSG_WIN_DAT */

extern MSG_WIN_DAT msg_win_data[4];     /* data 338838 */

#endif /* _GRAPHICS_GRAPH2D_MSG_WIN_DAT_H */
