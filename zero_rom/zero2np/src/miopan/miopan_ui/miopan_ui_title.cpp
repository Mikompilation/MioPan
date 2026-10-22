/*
 * The overlay's title screen window -- Settings > Title screen.
 *
 * Everything here is miopan_config.title, written in place: the overlay's
 * settle-and-save (PersistSettingsIfChanged() in miopan_ui.cpp) notices the
 * struct moved and writes miopan.ini half a second after the last change.
 * outgame/title_room.c reads the same struct every frame, so most edits --
 * the camera points, the path, the dim -- show on the next frame with nothing
 * to call.  The ones that need work done (a different room, a different
 * track) say so through title_room.h.
 *
 * The window is drawn from after_super(), on the game thread between one game
 * frame and the next, like every other overlay window that touches the game.
 */
#include "miopan_ui_title.h"

#include "../miopan_config.h"

#include "../../main/gphase.h"          /* GPhaseIsActive */
#include "../../outgame/title_room.h"

#include <imgui.h>

#include <cstdio>

namespace
{

bool g_show_title = false;
/* The room the last capture moved both points to, shown until the next one. */
int g_capture_moved_room = -1;

void Tooltip(const char *text)
{
    if (text != nullptr &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", text);
    }
}

void RoomLabel(int room, char *out, size_t size)
{
    std::snprintf(out, size, "%2d  %-6s %s", room, TitleRoomCode(room),
                  TitleRoomPlace(room));
}

void DrawStatus()
{
    const ImVec4 good(0.45f, 0.85f, 0.50f, 1.0f);
    const ImVec4 busy(1.00f, 0.78f, 0.35f, 1.0f);
    const ImVec4 bad(0.94f, 0.45f, 0.40f, 1.0f);
    const MioPan_ConfigTitle &t = miopan_config.title;

    if (GPhaseIsActive(GID_TITLE_MODE) == 0)
    {
        ImGui::TextDisabled("Not on the title screen. Changes show the next "
                            "time it comes up.");
        return;
    }

    switch (TitleRoomGetState())
    {
    case TITLE_ROOM_OFF:
        ImGui::TextDisabled("Showing the original background.");
        break;
    case TITLE_ROOM_LOADING:
        ImGui::TextColored(busy, "Loading %s...", TitleRoomCode(t.room));
        break;
    case TITLE_ROOM_SHOWN:
        ImGui::TextColored(good, "Showing %s, %s.", TitleRoomCode(t.room),
                           TitleRoomPlace(t.room));
        break;
    case TITLE_ROOM_RELEASED:
        ImGui::TextDisabled("This screen draws its own background; the room "
                            "comes back with the menu.");
        break;
    case TITLE_ROOM_MISSING:
        ImGui::TextColored(bad, "%s is not on this disc.", TitleRoomCode(t.room));
        break;
    }
}

void DrawBackdrop()
{
    MioPan_ConfigTitle &t = miopan_config.title;
    int backdrop = t.backdrop;

    ImGui::TextUnformatted("Backdrop");
    ImGui::SameLine();
    ImGui::RadioButton("A room", &backdrop, MIOPAN_TITLE_BACKDROP_ROOM);
    Tooltip("One of the game's rooms, lit and animated as the game plays it,\n"
            "behind the original logo and menu.");
    ImGui::SameLine();
    ImGui::RadioButton("The original background", &backdrop,
                       MIOPAN_TITLE_BACKDROP_CLASSIC);
    Tooltip("The scrolling art the original title draws.  The title is then\n"
            "exactly the original's, apart from the music choice below.");

    if (backdrop != t.backdrop)
    {
        t.backdrop = backdrop;
        TitleRoomReload();
    }
}

void DrawRoom()
{
    MioPan_ConfigTitle &t = miopan_config.title;
    const float em = ImGui::GetFontSize();
    char label[64];

    RoomLabel(t.room, label, sizeof(label));
    ImGui::SetNextItemWidth(em * 18.0f);
    if (ImGui::BeginCombo("Room", label, ImGuiComboFlags_HeightLarge))
    {
        for (int room = 0; room < TitleRoomCount(); room++)
        {
            const bool on_disc = TitleRoomIsOnDisc(room) != 0;
            RoomLabel(room, label, sizeof(label));

            if (ImGui::Selectable(label, room == t.room,
                                  on_disc ? 0 : ImGuiSelectableFlags_Disabled))
            {
                if (room != t.room)
                {
                    t.room = room;
                    t.camera_set = 0;
                    g_capture_moved_room = -1;
                    TitleRoomReload();
                }
            }
            if (room == t.room)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    Tooltip("Rooms by the disc's own file names.  A new room starts with the\n"
            "camera placed by itself; set the two points to taste after.\n"
            "Greyed rooms are not on this disc.");

    ImGui::SameLine();
    if (ImGui::Button("Reload"))
    {
        TitleRoomReload();
    }
    Tooltip("Load the room again from the disc.");
}

/* One camera point: capture, hold, and the three values by hand. */
void DrawPoint(int point, float *eye, float *target, float *fov)
{
    const float em = ImGui::GetFontSize();
    const bool in_game = GPhaseIsActive(GID_STORY_MAIN) != 0;
    const bool on_title = GPhaseIsActive(GID_TITLE_MODE) != 0;
    char id[16];

    std::snprintf(id, sizeof(id), "##point%d", point);
    ImGui::PushID(id);

    ImGui::SeparatorText(point == 0 ? "Point A" : "Point B");

    if (ImGui::Button("Set from the current view", ImVec2(0.0f, 0.0f)))
    {
        if (TitleRoomCaptureView(point) != 0)
        {
            g_capture_moved_room = miopan_config.title.room;
        }
    }
    Tooltip(in_game
                ? "Take the camera as it is now.  In a game the title's room\n"
                  "becomes the one Mio is in -- if that is another room, both\n"
                  "points take this view, to be moved apart from there.\n"
                  "Assists > Free camera frames a shot anywhere."
                : "Take the camera as it is now -- the path's, or the one you\n"
                  "are flying with \"Fly the camera\" below.");
    ImGui::SameLine();
    if (ImGui::Button("Look from here", ImVec2(0.0f, 0.0f)))
    {
        TitleRoomSetFly(0);
        TitleRoomSetPreview(TitleRoomGetPreview() == point ? -1 : point);
    }
    Tooltip("Hold the camera on this point; again to let the path run.");
    if (on_title && TitleRoomGetPreview() == point)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(holding)");
    }

    ImGui::SetNextItemWidth(em * 18.0f);
    ImGui::DragFloat3("Eye", eye, 5.0f, -100000.0f, 100000.0f, "%.0f");
    Tooltip("Where the camera is, in the game's world units.  Y grows\n"
            "downwards; a standing character is about a thousand tall.");
    ImGui::SetNextItemWidth(em * 18.0f);
    ImGui::DragFloat3("Looks at", target, 5.0f, -100000.0f, 100000.0f, "%.0f");
    ImGui::SetNextItemWidth(em * 18.0f);
    ImGui::SliderFloat("Field of view", fov, 10.0f, 120.0f, "%.1f deg");

    ImGui::PopID();
}

void DrawCamera()
{
    MioPan_ConfigTitle &t = miopan_config.title;
    const float em = ImGui::GetFontSize();

    static const char *const kMotions[] = {
        "There and back", "A to B, then cut back", "A to B, then hold",
    };
    static const char *const kEasings[] = { "Smooth", "Linear" };

    ImGui::SetNextItemWidth(em * 14.0f);
    ImGui::Combo("Path", &t.motion, kMotions, IM_ARRAYSIZE(kMotions));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 7.0f);
    ImGui::Combo("Easing", &t.easing, kEasings, IM_ARRAYSIZE(kEasings));
    Tooltip("Smooth eases out of each end and into the next.");

    ImGui::SetNextItemWidth(em * 7.0f);
    ImGui::DragFloat("Travel (s)", &t.travel_seconds, 0.1f, 0.5f, 600.0f, "%.1f");
    Tooltip("Seconds from A to B.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 7.0f);
    ImGui::DragFloat("Hold (s)", &t.hold_seconds, 0.1f, 0.0f, 600.0f, "%.1f");
    Tooltip("Seconds the camera rests at each end.");
    if (t.travel_seconds < 0.5f)
    {
        t.travel_seconds = 0.5f;
    }
    if (t.hold_seconds < 0.0f)
    {
        t.hold_seconds = 0.0f;
    }

    float progress = TitleRoomGetProgress();
    ImGui::SetNextItemWidth(em * 14.0f);
    if (ImGui::SliderFloat("Position", &progress, 0.0f, 1.0f, "%.2f"))
    {
        TitleRoomSetPreview(-1);
        TitleRoomSetProgress(progress);
    }
    Tooltip("Where along the path the camera is -- drag it to scrub.");
    ImGui::SameLine();
    bool paused = TitleRoomGetPaused() != 0;
    if (ImGui::Checkbox("Pause", &paused))
    {
        TitleRoomSetPaused(paused ? 1 : 0);
    }

    bool fly = TitleRoomGetFly() != 0;
    if (ImGui::Checkbox("Fly the camera", &fly))
    {
        TitleRoomSetPreview(-1);
        TitleRoomSetFly(fly ? 1 : 0);
    }
    Tooltip("Steer the camera by hand, then \"Set from the current view\".\n"
            "The free camera's controls, on the title screen:\n"
            "  look     aim stick, or W A S D\n"
            "  move     other stick or d-pad, or the arrow keys\n"
            "  up/down  R1 / L1, or F / R\n"
            "  speed    hold L2 for slow, R2 for fast\n"
            "The title menu still hears the pad, so fly from PRESS START.");
    ImGui::SameLine();
    if (ImGui::Button("Place it by itself"))
    {
        TitleRoomSetFly(0);
        TitleRoomSetPreview(-1);
        TitleRoomFrameAutomatically();
    }
    Tooltip("Put both points back where a new room starts them: along the\n"
            "long side at eye height, looking at the middle.");

    DrawPoint(0, t.a_eye, t.a_target, &t.a_fov);
    DrawPoint(1, t.b_eye, t.b_target, &t.b_fov);

    if (g_capture_moved_room >= 0)
    {
        ImGui::TextColored(ImVec4(1.00f, 0.78f, 0.35f, 1.0f),
                           "The title's room is now %s: both points took that "
                           "view.", TitleRoomCode(g_capture_moved_room));
    }
}

void DrawLook()
{
    MioPan_ConfigTitle &t = miopan_config.title;
    const float em = ImGui::GetFontSize();

    ImGui::SetNextItemWidth(em * 14.0f);
    ImGui::SliderFloat("Dim under the logo", &t.dim, 0.0f, 1.0f, "%.2f");
    Tooltip("Black laid over the room and under the logo and menu, to keep\n"
            "them readable over a bright room.");

    bool movie = t.attract_movie != 0;
    if (ImGui::Checkbox("Play the attract movie when idle", &movie))
    {
        t.attract_movie = movie ? 1 : 0;
    }
    Tooltip("The original rolls a movie after fifteen idle seconds on PRESS\n"
            "START.  Off keeps the room up.  The original background always\n"
            "plays it.");
}

void DrawMusic()
{
    MioPan_ConfigTitle &t = miopan_config.title;
    const float em = ImGui::GetFontSize();
    char label[96];
    const int current = TitleRoomMusicIndex(t.music);

    auto track_label = [&label](int index) {
        const char *note = TitleRoomMusicNote(index);
        if (note[0] != '\0')
        {
            std::snprintf(label, sizeof(label), "%s - %s",
                          TitleRoomMusicName(index), note);
        }
        else
        {
            std::snprintf(label, sizeof(label), "%s",
                          TitleRoomMusicName(index));
        }
        return label;
    };

    ImGui::SetNextItemWidth(em * 24.0f);
    if (ImGui::BeginCombo("Track", current >= 0 ? track_label(current) : "?",
                          ImGuiComboFlags_HeightLarge))
    {
        for (int i = 0; i < TitleRoomMusicCount(); i++)
        {
            if (ImGui::Selectable(track_label(i), i == current))
            {
                t.music = TitleRoomMusicFile(i);
                TitleRoomMusicChanged();
            }
            if (i == current)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    Tooltip("Original is the title theme the way the game plays it: once,\n"
            "and again whenever a menu finds it stopped.  Anything else\n"
            "loops for as long as the title is up, under every screen that\n"
            "plays the title music.  The KAN tracks are the rooms' own\n"
            "ambience.");

    ImGui::BeginDisabled(t.music == 0);
    ImGui::SetNextItemWidth(em * 14.0f);
    if (ImGui::SliderFloat("Volume", &t.music_volume, 0.0f, 1.0f, "%.2f"))
    {
        TitleRoomMusicChanged();
    }
    Tooltip("1 is the level the game plays its music at.");
    ImGui::EndDisabled();
}

} // namespace

namespace MioPanUi
{

void DrawTitleMenuItem()
{
    ImGui::MenuItem("Title screen...", nullptr, &g_show_title);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("The title screen's backdrop -- a room from the\n"
                          "game, or the original art -- its camera and its\n"
                          "music.");
    }
}

void DrawTitleWindow()
{
    if (!g_show_title)
    {
        return;
    }

    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 34.0f, em * 44.0f),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("MioPan Title Screen", &g_show_title))
    {
        ImGui::End();
        return;
    }

    DrawStatus();
    ImGui::Spacing();
    DrawBackdrop();

    ImGui::BeginDisabled(miopan_config.title.backdrop !=
                         MIOPAN_TITLE_BACKDROP_ROOM);
    if (ImGui::CollapsingHeader("Room", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawRoom();
    }
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawCamera();
    }
    if (ImGui::CollapsingHeader("Look", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawLook();
    }
    ImGui::EndDisabled();

    if (ImGui::CollapsingHeader("Music", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawMusic();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Saved to miopan.ini, [title].");

    ImGui::End();
}

} // namespace MioPanUi
