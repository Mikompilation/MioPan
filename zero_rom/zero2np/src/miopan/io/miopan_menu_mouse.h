#ifndef MIOPAN_MENU_MOUSE_H
#define MIOPAN_MENU_MOUSE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The mouse in the game's own menus.
 *
 * The ROM's menus read nothing but the pad.  This lets each of them take the
 * mouse too, without changing what they do: a menu's pad handler asks for
 * the mouse once a frame, moves its own cursor to whatever the pointer is
 * over, and has a click press CROSS as if the player had -- so the menu runs
 * the very code it runs for the pad, sounds and all.
 *
 * Coordinates are the ROM's 640x448 screen, the space every menu draws its
 * sprites in; on a widened output they run past 0 and 640, as the picture
 * does.  Hover only moves a cursor when the pointer has actually moved, so a
 * pointer left resting does not fight the keyboard or the pad.
 *
 * The game's Yes / No windows are answered by the layer itself, from what the
 * shared cells report as they draw (see below), so only screens with their own
 * kind of list need a hook.
 *
 * Asking for the mouse is what makes it the menu's.  While a menu asks -- this
 * frame or the last -- mouse buttons and the wheel stop reaching the game
 * through their bindings, right click presses TRIANGLE (the game's back), the
 * wheel presses the directions the menu says it scrolls in, and Enter and
 * Backspace are CROSS and TRIANGLE, the keys a PC player reaches for.  The
 * mouse is never the menu's while the cursor is captured for aiming, while the
 * ImGui overlay holds it or is under it, or while an RmlUi screen is up.
 */

/* Logical pad buttons, MioPan_InputButtonHeld()'s indices. */
#define MIOPAN_MENU_UP       0
#define MIOPAN_MENU_DOWN     1
#define MIOPAN_MENU_LEFT     2
#define MIOPAN_MENU_RIGHT    3
#define MIOPAN_MENU_TRIANGLE 4
#define MIOPAN_MENU_CROSS    5
#define MIOPAN_MENU_SQUARE   6
#define MIOPAN_MENU_CIRCLE   7
#define MIOPAN_MENU_L1       8
#define MIOPAN_MENU_L2       9
#define MIOPAN_MENU_R1       10
#define MIOPAN_MENU_R2       11
#define MIOPAN_MENU_START    12
#define MIOPAN_MENU_SELECT   13

/* What a menu asks for, MioPan_MenuMouseBegin()'s `flags`. */
#define MIOPAN_MM_WHEEL_VERTICAL   0x01  /* the wheel presses UP / DOWN       */
#define MIOPAN_MM_WHEEL_HORIZONTAL 0x02  /* the wheel presses LEFT / RIGHT    */
#define MIOPAN_MM_WHEEL_SHOULDERS  0x04  /* the wheel presses L1 / R1 (pages) */
#define MIOPAN_MM_NO_BACK          0x08  /* right click and Backspace do
                                            nothing: the screen has no back   */

typedef struct
{
    float x;        /* the pointer in the 640x448 screen                     */
    float y;
    int   inside;   /* it is over the game picture                          */
    int   moved;    /* it moved since the last frame                        */
    int   left;     /* the left button went down this frame                 */
    int   left_held;/* the left button is down                              */
    int   right;    /* the right button went down this frame                */
    int   wheel;    /* notches this frame, positive away from the player    */
} MioPan_MenuMouse;

/* Claim the mouse for this frame's menu and read it.  Returns 1 with `out`
 * filled when the menu has the mouse, 0 (and `out` zeroed) when it does not.
 * Call it once a frame from the menu's pad handler; `out` may be null. */
int MioPan_MenuMouseBegin(int flags, MioPan_MenuMouse *out);

/* The pointer is over the box, in 640x448 coordinates.  0 whenever the menu
 * does not have the mouse. */
int MioPan_MenuMouseIn(float x, float y, float w, float h);

/* Press a logical button (MIOPAN_MENU_*) for one pad read, as the player
 * would.  Presses of the same button are spaced so each is its own edge. */
void MioPan_MenuMousePress(int button);

/*
 * The usual menu: `count` boxes, box i at (x + i*dx, y + i*dy) and w x h.
 * Hovering one with the pointer moving moves the cursor there, with the
 * game's cursor tick; clicking one moves the cursor there and presses CROSS.
 * Returns the cursor to store -- `cursor` itself when nothing changed -- and
 * sets *hit to the box under the pointer, or -1 (hit may be null).  Boxes
 * whose index `skip` says no to are left alone (skip may be null).
 */
int MioPan_MenuMouseList(int cursor, float x, float y, float w, float h,
                         float dx, float dy, int count, int *hit,
                         int (*skip)(int index));

/* One item of a menu whose items are not evenly spaced: the same as one box
 * of MioPan_MenuMouseList(), for item `index`. */
int MioPan_MenuMouseItem(int cursor, int index, float x, float y, float w,
                         float h);

/*
 * The same for a menu whose cursor has to be moved the pad's way -- a list
 * that scrolls, re-cues a caption or cross-fades a picture as its cursor
 * moves.  Hovering box i steps the cursor there with presses of `dec` /
 * `inc` (MIOPAN_MENU_UP / DOWN, say), so the menu runs its own movement code;
 * clicking steps there and then presses CROSS.  `cursor` is the box the
 * cursor is on now.  Returns the box under the pointer, or -1.
 */
int MioPan_MenuMouseSeekList(int cursor, float x, float y, float w, float h,
                             float dx, float dy, int count, int dec, int inc);

/* Two axes of the same: `cols` x `rows` cells, cell (c, r) at
 * (x + c*dx, y + r*dy), stepped to with UP / DOWN along a column and LEFT /
 * RIGHT across a row.  A click steps there and presses `confirm` (-1 for
 * nothing).  Returns the cell under the pointer as r * cols + c, or -1. */
int MioPan_MenuMouseSeekGrid(int col, int row, float x, float y, float w,
                             float h, float dx, float dy, int cols, int rows,
                             int confirm);

/* A single target: clicking the box presses `button`.  Returns 1 when the
 * pointer is over it. */
int MioPan_MenuMouseButton(float x, float y, float w, float h, int button);

/* A screen that only waits to be acknowledged ("saved", "no memory card"):
 * a click anywhere presses CROSS, and right click TRIANGLE as everywhere. */
void MioPan_MenuMouseConfirm(void);
/* A screen whose only way is out: a click anywhere presses TRIANGLE. */
void MioPan_MenuMouseBack(void);

/* The left button went down this frame and no target has taken it yet;
 * taking it here.  For a screen where a click anywhere means one thing. */
int MioPan_MenuMouseClicked(void);

/* ---- what the game's shared widgets report as they draw ----------------
 *
 * Every Yes / No in the game is drawn by draw_cmn_win.c's cells, so those
 * report themselves here and the layer answers them on its own -- the
 * screens that ask need no code of their own.  A Yes / No drawn last frame is
 * the one on screen now: pointing at the other answer steps the cursor there
 * and a click answers. */
void MioPan_MenuMouseNoteYesNo(int which, float x, float y);   /* 0 yes, 1 no */
void MioPan_MenuMouseNoteCursor(float x, float y, float w);     /* DrawCmnSelCsr */

/* The button hints along the top or bottom of a menu ("X Select  /\ Back"),
 * drawn by draw_caption.c: each reports its box and the button it names, and
 * a click in a box drawn last frame presses that button.  A screen that shows
 * hints is a menu, so it has the mouse even without a hook of its own. */
void MioPan_MenuMouseNoteHint(int button, float x, float y, float w, float h);

/* ---- for the input layer ---------------------------------------------- */

union SDL_Event;
struct SDL_Window;

/* Every event, from MioPan_InputProcessEvent().  `free` says whether the
 * pointer is free for the menus: not captured, not the overlay's, and the
 * game not suppressed.  Returns 1 when the event was the menus' to take. */
int  MioPan_MenuMouseEvent(const union SDL_Event *event, int free);
/* Once per pad read, before the game runs: hand the gathered presses to the
 * frame and roll the claim over. */
void MioPan_MenuMousePoll(struct SDL_Window *window);
/* A virtual press for logical button `index` this pad read. */
int  MioPan_MenuMouseHeld(int index);
/* A menu has the mouse, this frame or the last. */
int  MioPan_MenuMouseClaimed(void);
/* Provided by miopan_input.cpp: the pointer is free for the menus right now
 * -- not captured, not the overlay's, the game not suppressed. */
int  MioPan_MenuMouseFree(void);

#ifdef __cplusplus
}
#endif

#endif
