/* ==========================================================================
 *  graphics/graph2d/msg_col_tbl.h
 *
 *  Declares the message colour palette msg_col[] (defined in msg_col_tbl.c):
 *  the per-colour-label RGBA entries the message renderer resolves colour
 *  escape codes against.
 *
 *  msg_col_tbl.c (the initialised table) is not reconstructed yet; this header
 *  lets consumers link against a real declaration instead of an inline extern.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_GRAPH2D_MSG_COL_TBL_H
#define _GRAPHICS_GRAPH2D_MSG_COL_TBL_H

#include "message.h"                /* MSG_COLOR */

extern MSG_COLOR msg_col[45];       /* rdata 3c1ac0 */

#endif /* _GRAPHICS_GRAPH2D_MSG_COL_TBL_H */
