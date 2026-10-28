#include "miopan_menu_mouse.h"

#include "../rendering/miopan_renderer.h"
#include "../../system/os/system.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <string.h>

/* Presses waiting their turn.  Each takes two pad reads, down then up, so a
 * fast flick of the wheel is capped rather than playing out for a second
 * after the hand has stopped. */
#define MENU_MOUSE_QUEUE_MAX 16
#define MENU_MOUSE_BUTTONS   16

/* The Yes / No cells (DrawCmnSelYes / No: btn_tex 7 and 8, 126 wide with the
 * frame's bottom piece 23 below the top) and the unstretched selection cursor
 * (DrawCmnSelCsr with w 0: btn_tex 9 and 10, 61 each). */
#define YES_NO_W    126.0f
#define YES_NO_H    29.0f
#define SEL_CSR_W   122.0f

/* Button hints on screen at once: the busiest group has four buttons, the
 * shoulder pairs count twice, and a screen may draw two groups. */
#define MENU_MOUSE_HINT_MAX 16

namespace
{
struct YesNoNote
{
    int   drawn;        /* both cells drawn this frame */
    int   have_yes;
    int   have_no;
    float yes_x;
    float no_x;
    float y;
    int   cursor;       /* 0 yes, 1 no, -1 not drawn this frame */
};

struct HintNote
{
    int   button;
    float x;
    float y;
    float w;
    float h;
};

struct HintNotes
{
    HintNote hint[MENU_MOUSE_HINT_MAX];
    int      count;
};

struct MenuMouseState
{
    /* Gathered from events since the last read. */
    int pending_left;
    int pending_right;
    int pending_wheel_x;
    int pending_wheel_y;
    int pending_enter;
    int pending_back;
    int pending_moved;
    float wheel_travel_x;
    float wheel_travel_y;

    /* The pointer, in window points, from the last event that placed it. */
    float pointer_x;
    float pointer_y;
    SDL_WindowID pointer_window;
    int pointer_valid;
    int left_down;

    /* What this frame's menu sees. */
    MioPan_MenuMouse frame;
    int available;

    /* The menu that asked last, and the one before it. */
    int claimed;
    int claimed_prev;
    int claim_flags;

    /* Presses in order, and the one down for the read now running. */
    int queue[MENU_MOUSE_QUEUE_MAX];
    int queue_len;
    int active;             /* button + 1, 0 for none */
    int releasing;          /* the read after a press, everything up */

    /* The game's own Yes / No, as drawn last frame and this one. */
    YesNoNote yes_no_drawing;
    YesNoNote yes_no;

    /* The button hints, the same way. */
    HintNotes hints_drawing;
    HintNotes hints;
};

MenuMouseState g_mm;

void Queue(int button, int count)
{
    if (button < 0 || button >= MENU_MOUSE_BUTTONS)
    {
        return;
    }
    for (int i = 0; i < count && g_mm.queue_len < MENU_MOUSE_QUEUE_MAX; i++)
    {
        g_mm.queue[g_mm.queue_len++] = button;
    }
}

bool Busy()
{
    return g_mm.queue_len != 0 || g_mm.active != 0;
}

/* Where the presses already made will have moved a cursor stepped by `dec` /
 * `inc`: the one down this read has not reached the menu yet when its pad
 * handler asks, and the queued ones are still to come. */
int PendingSteps(int dec, int inc, bool queued)
{
    int steps = 0;
    if (g_mm.active == inc + 1)
    {
        steps++;
    }
    else if (g_mm.active == dec + 1)
    {
        steps--;
    }
    for (int i = 0; queued && i < g_mm.queue_len; i++)
    {
        steps += g_mm.queue[i] == inc ? 1 : g_mm.queue[i] == dec ? -1 : 0;
    }
    return steps;
}

/* Re-aim: drop the steps still queued and queue the ones from where the
 * cursor will be once the press now down has landed. */
void SeekTo(int cursor, int target, int dec, int inc)
{
    int kept = 0;
    for (int i = 0; i < g_mm.queue_len; i++)
    {
        if (g_mm.queue[i] != dec && g_mm.queue[i] != inc)
        {
            g_mm.queue[kept++] = g_mm.queue[i];
        }
    }
    g_mm.queue_len = kept;
    const int steps = target - (cursor + PendingSteps(dec, inc, false));
    Queue(steps > 0 ? inc : dec, steps > 0 ? steps : -steps);
}

/* The wheel as presses, per the flags of the menu that has it. */
void QueueWheel(int flags, int x, int y)
{
    const int notches = y > 0 ? y : -y;
    if (y != 0)
    {
        if (flags & MIOPAN_MM_WHEEL_VERTICAL)
        {
            Queue(y > 0 ? MIOPAN_MENU_UP : MIOPAN_MENU_DOWN, notches);
        }
        else if (flags & MIOPAN_MM_WHEEL_HORIZONTAL)
        {
            Queue(y > 0 ? MIOPAN_MENU_LEFT : MIOPAN_MENU_RIGHT, notches);
        }
        else if (flags & MIOPAN_MM_WHEEL_SHOULDERS)
        {
            Queue(y > 0 ? MIOPAN_MENU_L1 : MIOPAN_MENU_R1, notches);
        }
    }
    if (x != 0 && (flags & MIOPAN_MM_WHEEL_HORIZONTAL))
    {
        Queue(x < 0 ? MIOPAN_MENU_LEFT : MIOPAN_MENU_RIGHT, x < 0 ? -x : x);
    }
}

/* The pointer, in the ROM's 640x448 screen: window points to output pixels,
 * then the picture's place in the output, then the 2D frame's place in the
 * picture -- MioPan_RendererGetViewBounds(), which is how far the widened
 * screen runs past 0 and 640.  The position is the last event's rather than
 * SDL's polled state, which is the same thing for a real mouse and lets an
 * injected event be a pointer too. */
void ReadPointer(MioPan_MenuMouse *out)
{
    SDL_Window *window = g_mm.pointer_valid
                             ? SDL_GetWindowFromID(g_mm.pointer_window)
                             : nullptr;
    if (window == nullptr)
    {
        return;
    }
    const float wx = g_mm.pointer_x;
    const float wy = g_mm.pointer_y;
    float density = SDL_GetWindowPixelDensity(window);
    if (!(density > 0.0f))
    {
        density = 1.0f;
    }

    int rx = 0;
    int ry = 0;
    int rw = 0;
    int rh = 0;
    MioPan_RendererGetPresentRect(&rx, &ry, &rw, &rh);
    if (rw <= 0 || rh <= 0)
    {
        SDL_GetWindowSizeInPixels(window, &rw, &rh);
        rx = 0;
        ry = 0;
    }
    if (rw <= 0 || rh <= 0)
    {
        return;
    }

    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 640.0f;
    float y1 = 448.0f;
    MioPan_RendererGetViewBounds(&x0, &y0, &x1, &y1);

    const float px = wx * density - (float)rx;
    const float py = wy * density - (float)ry;
    out->x = x0 + px / (float)rw * (x1 - x0);
    out->y = y0 + py / (float)rh * (y1 - y0);
    out->inside = px >= 0.0f && py >= 0.0f && px < (float)rw && py < (float)rh;
    out->left_held = g_mm.left_down;
}

void PlayCursor()
{
    SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);
}

/*
 * The game's Yes / No, wherever it is.  Every screen that asks one draws it
 * with the same cells, so a Yes / No drawn last frame is one the player is
 * looking at now, whichever screen asked it.  Pointing at the other answer
 * steps the cursor to it -- LEFT toward Yes, RIGHT toward No, which both kinds
 * of handler the game has (toggle, or set) take the same way -- and a click
 * answers.  Only when the cursor's own cell was drawn too, so the screen's
 * answer is known: stepping blind could flip it the wrong way.
 */
void HandleYesNo()
{
    const YesNoNote &yn = g_mm.yes_no;
    if (!yn.drawn || yn.cursor < 0 || !MioPan_MenuMouseFree() ||
        !g_mm.frame.inside)
    {
        return;
    }
    g_mm.claimed = 1;
    g_mm.claim_flags = 0;
    g_mm.available = 1;

    int target = -1;
    if (MioPan_MenuMouseIn(yn.yes_x, yn.y - 2.0f, YES_NO_W, YES_NO_H))
    {
        target = 0;
    }
    else if (MioPan_MenuMouseIn(yn.no_x, yn.y - 2.0f, YES_NO_W, YES_NO_H))
    {
        target = 1;
    }
    if (target < 0 || Busy())
    {
        return;
    }
    if (target != yn.cursor && (g_mm.frame.moved || g_mm.frame.left))
    {
        Queue(target == 0 ? MIOPAN_MENU_LEFT : MIOPAN_MENU_RIGHT, 1);
    }
    if (g_mm.frame.left)
    {
        g_mm.frame.left = 0;
        Queue(MIOPAN_MENU_CROSS, 1);
    }
}

/* The button hints drawn last frame: a click on one presses its button.  The
 * menu under them has the mouse whether or not it has a hook. */
void HandleHints()
{
    const HintNotes &hn = g_mm.hints;
    if (hn.count == 0 || !MioPan_MenuMouseFree() || !g_mm.frame.inside)
    {
        return;
    }
    g_mm.claimed = 1;
    g_mm.claim_flags = 0;
    g_mm.available = 1;
    if (!g_mm.frame.left)
    {
        return;
    }
    for (int i = 0; i < hn.count; i++)
    {
        const HintNote &h = hn.hint[i];
        if (MioPan_MenuMouseIn(h.x, h.y, h.w, h.h))
        {
            g_mm.frame.left = 0;
            Queue(h.button, 1);
            return;
        }
    }
}
}

extern "C" {

int MioPan_MenuMouseEvent(const SDL_Event *event, int free)
{
    if (event == nullptr)
    {
        return 0;
    }
    switch (event->type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        /* Touch is not a pointer, and zero-length motion is SDL putting the
         * cursor back after relative mode, not the player. */
        if (event->motion.which == SDL_TOUCH_MOUSEID)
        {
            return 0;
        }
        g_mm.pointer_x = event->motion.x;
        g_mm.pointer_y = event->motion.y;
        g_mm.pointer_window = event->motion.windowID;
        g_mm.pointer_valid = 1;
        if (event->motion.xrel != 0.0f || event->motion.yrel != 0.0f)
        {
            g_mm.pending_moved = 1;
        }
        return 0;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.which != SDL_TOUCH_MOUSEID &&
            event->button.button == SDL_BUTTON_LEFT)
        {
            g_mm.left_down = 0;
        }
        return 0;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        g_mm.pointer_valid = 0;
        return 0;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.which == SDL_TOUCH_MOUSEID)
        {
            return 0;
        }
        g_mm.pointer_x = event->button.x;
        g_mm.pointer_y = event->button.y;
        g_mm.pointer_window = event->button.windowID;
        g_mm.pointer_valid = 1;
        if (event->button.button == SDL_BUTTON_LEFT)
        {
            g_mm.left_down = 1;
        }
        if (!free || MioPan_MenuMouseClaimed() == 0)
        {
            return 0;
        }
        if (event->button.button == SDL_BUTTON_LEFT)
        {
            g_mm.pending_left++;
        }
        else if (event->button.button == SDL_BUTTON_RIGHT)
        {
            g_mm.pending_right++;
        }
        return 1;
    case SDL_EVENT_MOUSE_WHEEL:
    {
        if (!free || event->wheel.which == SDL_TOUCH_MOUSEID ||
            MioPan_MenuMouseClaimed() == 0)
        {
            return 0;
        }
        float x = event->wheel.x;
        float y = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
        {
            x = -x;
            y = -y;
        }
        g_mm.wheel_travel_x += x;
        g_mm.wheel_travel_y += y;
        while (g_mm.wheel_travel_y >= 1.0f)
        {
            g_mm.pending_wheel_y++;
            g_mm.wheel_travel_y -= 1.0f;
        }
        while (g_mm.wheel_travel_y <= -1.0f)
        {
            g_mm.pending_wheel_y--;
            g_mm.wheel_travel_y += 1.0f;
        }
        while (g_mm.wheel_travel_x >= 1.0f)
        {
            g_mm.pending_wheel_x++;
            g_mm.wheel_travel_x -= 1.0f;
        }
        while (g_mm.wheel_travel_x <= -1.0f)
        {
            g_mm.pending_wheel_x--;
            g_mm.wheel_travel_x += 1.0f;
        }
        return 1;
    }
    case SDL_EVENT_KEY_DOWN:
        if (!free || event->key.repeat || MioPan_MenuMouseClaimed() == 0)
        {
            return 0;
        }
        if (event->key.scancode == SDL_SCANCODE_RETURN ||
            event->key.scancode == SDL_SCANCODE_KP_ENTER)
        {
            g_mm.pending_enter++;
            return 1;
        }
        if (event->key.scancode == SDL_SCANCODE_BACKSPACE)
        {
            g_mm.pending_back++;
            return 1;
        }
        return 0;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        g_mm.left_down = 0;
        g_mm.queue_len = 0;
        g_mm.active = 0;
        g_mm.pending_left = g_mm.pending_right = 0;
        g_mm.pending_enter = g_mm.pending_back = 0;
        g_mm.pending_wheel_x = g_mm.pending_wheel_y = 0;
        return 0;
    default:
        return 0;
    }
}

void MioPan_MenuMousePoll(SDL_Window *window)
{
    (void)window;

    /* What the menu that just ran asked for turns into presses now. */
    if (MioPan_MenuMouseClaimed() != 0)
    {
        const int flags = g_mm.claim_flags;
        if ((flags & MIOPAN_MM_NO_BACK) == 0)
        {
            Queue(MIOPAN_MENU_TRIANGLE, g_mm.pending_right + g_mm.pending_back);
        }
        Queue(MIOPAN_MENU_CROSS, g_mm.pending_enter);
        QueueWheel(flags, g_mm.pending_wheel_x, g_mm.pending_wheel_y);
    }

    MioPan_MenuMouse frame;
    memset(&frame, 0, sizeof(frame));
    ReadPointer(&frame);
    frame.moved = g_mm.pending_moved;
    frame.left = g_mm.pending_left > 0;
    frame.right = g_mm.pending_right > 0;
    frame.wheel = g_mm.pending_wheel_y;
    g_mm.frame = frame;
    g_mm.available = 0;

    g_mm.pending_left = g_mm.pending_right = 0;
    g_mm.pending_enter = g_mm.pending_back = 0;
    g_mm.pending_wheel_x = g_mm.pending_wheel_y = 0;
    g_mm.pending_moved = 0;

    g_mm.claimed_prev = g_mm.claimed;
    g_mm.claimed = 0;

    /* Last frame's drawing is this frame's screen. */
    g_mm.yes_no = g_mm.yes_no_drawing;
    g_mm.yes_no.drawn = g_mm.yes_no.have_yes && g_mm.yes_no.have_no;
    memset(&g_mm.yes_no_drawing, 0, sizeof(g_mm.yes_no_drawing));
    g_mm.yes_no_drawing.cursor = -1;
    g_mm.hints = g_mm.hints_drawing;
    g_mm.hints_drawing.count = 0;
    HandleYesNo();
    HandleHints();

    /* Down for one read, all up for the next, so presses in a row -- the
     * same button or not -- are each their own edge. */
    if (g_mm.active != 0)
    {
        g_mm.active = 0;
    }
    else if (g_mm.queue_len > 0)
    {
        g_mm.active = g_mm.queue[0] + 1;
        g_mm.queue_len--;
        memmove(g_mm.queue, g_mm.queue + 1, sizeof(int) * (size_t)g_mm.queue_len);
    }
}

int MioPan_MenuMouseHeld(int index)
{
    return g_mm.active != 0 && g_mm.active - 1 == index ? 1 : 0;
}

int MioPan_MenuMouseClaimed(void)
{
    return (g_mm.claimed != 0 || g_mm.claimed_prev != 0) ? 1 : 0;
}

int MioPan_MenuMouseBegin(int flags, MioPan_MenuMouse *out)
{
    const int free = MioPan_MenuMouseFree() != 0;
    g_mm.available = free && g_mm.frame.inside != 0;
    if (free)
    {
        g_mm.claimed = 1;
        g_mm.claim_flags = flags;
    }
    if (out != nullptr)
    {
        if (g_mm.available)
        {
            *out = g_mm.frame;
        }
        else
        {
            memset(out, 0, sizeof(*out));
        }
    }
    return g_mm.available;
}

int MioPan_MenuMouseIn(float x, float y, float w, float h)
{
    return g_mm.available && g_mm.frame.x >= x && g_mm.frame.y >= y &&
           g_mm.frame.x < x + w && g_mm.frame.y < y + h;
}

void MioPan_MenuMousePress(int button)
{
    Queue(button, 1);
}

int MioPan_MenuMouseItem(int cursor, int index, float x, float y, float w,
                         float h)
{
    if (!MioPan_MenuMouseIn(x, y, w, h))
    {
        return cursor;
    }
    if (g_mm.frame.left)
    {
        /* Taken, so a second target under the same click does not act on
         * it too. */
        g_mm.frame.left = 0;
        MioPan_MenuMousePress(MIOPAN_MENU_CROSS);
        return index;
    }
    if (g_mm.frame.moved && index != cursor)
    {
        PlayCursor();
        return index;
    }
    return cursor;
}

int MioPan_MenuMouseList(int cursor, float x, float y, float w, float h,
                         float dx, float dy, int count, int *hit,
                         int (*skip)(int index))
{
    if (hit != nullptr)
    {
        *hit = -1;
    }
    for (int i = 0; g_mm.available && i < count; i++)
    {
        const float bx = x + (float)i * dx;
        const float by = y + (float)i * dy;
        if ((skip != nullptr && skip(i) != 0) ||
            !MioPan_MenuMouseIn(bx, by, w, h))
        {
            continue;
        }
        if (hit != nullptr)
        {
            *hit = i;
        }
        return MioPan_MenuMouseItem(cursor, i, bx, by, w, h);
    }
    return cursor;
}

int MioPan_MenuMouseSeekList(int cursor, float x, float y, float w, float h,
                             float dx, float dy, int count, int dec, int inc)
{
    int hit = -1;
    for (int i = 0; g_mm.available && i < count && hit < 0; i++)
    {
        if (MioPan_MenuMouseIn(x + (float)i * dx, y + (float)i * dy, w, h))
        {
            hit = i;
        }
    }
    if (hit < 0)
    {
        return -1;
    }
    if (g_mm.frame.left)
    {
        g_mm.frame.left = 0;
        SeekTo(cursor, hit, dec, inc);
        Queue(MIOPAN_MENU_CROSS, 1);
    }
    else if (g_mm.frame.moved &&
             hit != cursor + PendingSteps(dec, inc, true))
    {
        SeekTo(cursor, hit, dec, inc);
    }
    return hit;
}

int MioPan_MenuMouseSeekGrid(int col, int row, float x, float y, float w,
                             float h, float dx, float dy, int cols, int rows,
                             int confirm)
{
    int hit_col = -1;
    int hit_row = -1;
    for (int r = 0; g_mm.available && r < rows && hit_row < 0; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            if (MioPan_MenuMouseIn(x + (float)c * dx, y + (float)r * dy, w, h))
            {
                hit_col = c;
                hit_row = r;
                break;
            }
        }
    }
    if (hit_row < 0)
    {
        return -1;
    }
    const int up = MIOPAN_MENU_UP;
    const int down = MIOPAN_MENU_DOWN;
    const int left = MIOPAN_MENU_LEFT;
    const int right = MIOPAN_MENU_RIGHT;
    if (g_mm.frame.left)
    {
        g_mm.frame.left = 0;
        SeekTo(row, hit_row, up, down);
        SeekTo(col, hit_col, left, right);
        if (confirm >= 0)
        {
            Queue(confirm, 1);
        }
    }
    else if (g_mm.frame.moved &&
             (hit_row != row + PendingSteps(up, down, true) ||
              hit_col != col + PendingSteps(left, right, true)))
    {
        SeekTo(row, hit_row, up, down);
        SeekTo(col, hit_col, left, right);
    }
    return hit_row * cols + hit_col;
}

int MioPan_MenuMouseButton(float x, float y, float w, float h, int button)
{
    if (!MioPan_MenuMouseIn(x, y, w, h))
    {
        return 0;
    }
    if (g_mm.frame.left)
    {
        g_mm.frame.left = 0;
        MioPan_MenuMousePress(button);
    }
    return 1;
}

void MioPan_MenuMouseConfirm(void)
{
    if (MioPan_MenuMouseBegin(0, nullptr) != 0 && MioPan_MenuMouseClicked() != 0)
    {
        MioPan_MenuMousePress(MIOPAN_MENU_CROSS);
    }
}

void MioPan_MenuMouseBack(void)
{
    if (MioPan_MenuMouseBegin(0, nullptr) != 0 && MioPan_MenuMouseClicked() != 0)
    {
        MioPan_MenuMousePress(MIOPAN_MENU_TRIANGLE);
    }
}

int MioPan_MenuMouseClicked(void)
{
    if (!g_mm.available || !g_mm.frame.left)
    {
        return 0;
    }
    g_mm.frame.left = 0;
    return 1;
}

void MioPan_MenuMouseNoteYesNo(int which, float x, float y)
{
    YesNoNote &yn = g_mm.yes_no_drawing;
    if (which == 0)
    {
        yn.have_yes = 1;
        yn.yes_x = x;
    }
    else
    {
        yn.have_no = 1;
        yn.no_x = x;
    }
    yn.y = y;
}

void MioPan_MenuMouseNoteCursor(float x, float y, float w)
{
    YesNoNote &yn = g_mm.yes_no_drawing;
    /* An unstretched cursor beside a Yes or No cell is the Yes / No's
     * cursor; which cell it sits on is the screen's answer.  The cells may
     * be drawn before or after it, so the match waits for the poll. */
    if (w != 0.0f)
    {
        return;
    }
    yn.cursor = x + SEL_CSR_W * 0.5f < 320.0f ? 0 : 1;
    (void)y;
}

void MioPan_MenuMouseNoteHint(int button, float x, float y, float w, float h)
{
    HintNotes &hn = g_mm.hints_drawing;
    if (hn.count < MENU_MOUSE_HINT_MAX && button >= 0 &&
        button < MENU_MOUSE_BUTTONS)
    {
        hn.hint[hn.count++] = {button, x, y, w, h};
    }
}
}
