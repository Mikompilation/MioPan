/* ==========================================================================
 *  graphics/graph2d/msg_disp_dat.h
 *
 *  Declares the per-message-type display table msg_disp_data[] (defined in
 *  msg_disp_dat.c): the (msg_def_id, msg_win_id) pair each message type maps to
 *  for its default position record and window rect.
 *
 *  msg_disp_dat.c (the initialised table) is not reconstructed yet; this header
 *  lets consumers link against a real declaration instead of an inline extern.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_GRAPH2D_MSG_DISP_DAT_H
#define _GRAPHICS_GRAPH2D_MSG_DISP_DAT_H

#include "message.h"                /* MSG_DISP_DATA */

extern MSG_DISP_DATA msg_disp_data[83];     /* data 3385a0 */

#endif /* _GRAPHICS_GRAPH2D_MSG_DISP_DAT_H */
