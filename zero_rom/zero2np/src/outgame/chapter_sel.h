/* ==========================================================================
 *  outgame/chapter_sel.h
 *
 *  Debug chapter-select screen: the developer entry point that skips the
 *  title flow and boots straight into a chosen chapter, costume, accessory
 *  pair and difficulty.  Reached from the title menu (GID_TITLE_CHAPTER_SEL)
 *  and exits into GID_STORY_LOAD_MISSION.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_CHAPTER_SEL_H
#define _OUTGAME_CHAPTER_SEL_H

void ChapterSelCtrlInit(void);
void ChapterSelMain(void);
void ChapterSelDispMain(void);

#endif /* _OUTGAME_CHAPTER_SEL_H */
