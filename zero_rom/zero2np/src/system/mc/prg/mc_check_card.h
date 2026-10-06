/* ==========================================================================
 *  system/mc/prg/mc_check_card.h
 *
 *  sceMcGetInfo() wrapper and the "is a usable PS2 card still in the slot"
 *  watch (mc_check_card.o, .text 0x1df418).
 *
 *  The card's own facts -- port, slot, type, free clusters, format flag -- are
 *  cached in this module's MC_INFO and read back through the five accessors
 *  below.  Everything else in system/mc that needs to know how much room is
 *  left goes through GetAccessMemoryCardFreeCluster().
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_CHECK_CARD_H
#define _SYSTEM_MC_PRG_MC_CHECK_CARD_H

/* Reset the cache to "no card": port and slot both -1.  MemoryCardExeInit()
 * calls this as a screen takes the card. */
void MemoryCardInfoCtrlInit(void);                                  /* 0x1df418 */

/* One-shot query of port / slot.  Main() returns 1 when the card is a
 * formatted PS2 card, -2 when it is unformatted (but only on the second look --
 * see the unformat_flg note in the .c), and the usual negatives otherwise. */
void MemoryCardGetCardInfoInit(int port, int slot);                 /* 0x1df458 */
int  MemoryCardGetCardInfoMain(void);                               /* 0x1df4a8 */
int  MemoryCardGetCardInfoReq(int port, int slot);                   /* 0x1df688 */

/* The same query, re-armed automatically.  A screen arms this once and polls
 * it every frame: 0 means nothing changed, 1 means the query completed and was
 * re-armed, and a negative is the card's complaint (-1 = the card was swapped,
 * which the screens treat as "start over"). */
void MemoryCardCheckEveryFrameInit(int port, int slot);             /* 0x1df480 */
int  MemoryCardCheckEveryFrameMain(void);                          /* 0x1df6b8 */

/* Cached card facts.  Port and slot answer -1 when they hold anything but 0
 * or 1, so a caller can test them without knowing whether a card was ever
 * queried; type, free and format are returned raw. */
int  GetAccessMemoryCardPort(void);                                /* 0x1df708 */
int  GetAccessMemoryCardSlot(void);                                /* 0x1df720 */
int  GetAccessMemoryCardType(void);                                /* 0x1df738 */
int  GetAccessMemoryCardFreeCluster(void);                         /* 0x1df748 */
int  GetAccessMemoryCardFormat(void);                              /* 0x1df758 */

/* Latch which port the next card operation runs against, without querying it.
 * The boot probe uses this to name port 0 before it has read anything. */
void MemoryCardSetAccessPort(int port);                            /* 0x1df768 */

#endif /* _SYSTEM_MC_PRG_MC_CHECK_CARD_H */
