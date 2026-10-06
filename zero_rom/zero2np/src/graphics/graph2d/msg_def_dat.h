/* ==========================================================================
 *  graphics/graph2d/msg_def_dat.h
 *
 *  Declares the default message-position table msg_def_data[] (defined in
 *  msg_def_dat.c): the per-def-id default (x,y) / layout the message system
 *  falls back to when a message type does not override its position.
 *
 *  msg_def_dat.c (the initialised table) is not reconstructed yet; this header
 *  lets consumers link against a real declaration instead of an inline extern.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_GRAPH2D_MSG_DEF_DAT_H
#define _GRAPHICS_GRAPH2D_MSG_DEF_DAT_H

#include "message.h"                /* MSG_DEF_DATA */

extern MSG_DEF_DATA msg_def_data[4];    /* data 338580 */

#endif /* _GRAPHICS_GRAPH2D_MSG_DEF_DAT_H */
