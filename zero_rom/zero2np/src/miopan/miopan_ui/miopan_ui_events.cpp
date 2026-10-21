/*
 * The overlay's event browser: every event in the game, chapter by chapter,
 * with a way to get to each.
 *
 * Story events sit under their chapter as the event tree has them -- the
 * chapter's load event, then its parts in order, then the story beats inside
 * each part.  The room events (ev2's tree: examine points, doors, pick-ups,
 * which run in every chapter) are a second tab, by room, and the handful that
 * belong to no chapter or room a third.
 *
 * Two ways to get to an event:
 *
 *   Jump  restart its chapter, settle everything it waits on, and take Mio to
 *         where it starts.  Story events only.
 *   Go    take Mio to where it starts in the game as it stands -- nothing else
 *         about the game changes.
 *
 * and, on the right-click menu, the ROM's own forced state changes for when a
 * condition will not come true by itself.  miopan_event_jump.cpp holds all of
 * it; this file only draws.
 */
#include "miopan_ui_events.h"

#include "miopan_event_jump.h"

#include "../../ingame/event/prg/ev_get.h"  /* EV_TBL_OPEN_COND / EV_TBL_CLOSE_COND */
#include "../../ingame/event/prg/ev_main.h" /* EV_STATE_*                          */

#include <imgui.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>

namespace
{

using MioPanEvents::Catalogue;
using MioPanEvents::Event;

bool g_show_events = false;
char g_filter[64] = "";
bool g_hide_flags = true;
int g_selected = -1;

void Tooltip(const char *text)
{
    if (text != nullptr &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", text);
    }
}

std::string Lower(const char *text)
{
    std::string out = text;
    for (char &c : out)
    {
        c = (char)std::tolower((unsigned char)c);
    }
    return out;
}

/* An event with no program and no sub-events is a flag other events test --
 * nothing to see, nothing to jump to. */
bool IsFlag(const Event &ev)
{
    return ev.subs.empty() && !ev.has_program;
}

ImVec4 StateColour(int state, bool armed)
{
    if (armed)
    {
        return ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
    }
    switch (state)
    {
    case EV_STATE_INIT:
    case EV_STATE_EXE:
    case EV_STATE_WAIT_END:
        return ImVec4(1.00f, 0.78f, 0.35f, 1.0f);
    case EV_STATE_SUSPEND:
        return ImVec4(0.60f, 0.65f, 0.90f, 1.0f);
    case EV_STATE_END:
        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    case EV_STATE_LOCK:
        return ImVec4(0.75f, 0.45f, 0.45f, 1.0f);
    default:
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

/* What the rows need to know once a frame rather than once a row. */
struct Rows
{
    const Catalogue &cat;
    bool can_jump;
    const char *jump_why;
    bool can_go;
    const char *go_why;
    std::string filter;
};

void EventTooltip(const Catalogue &cat, int id)
{
    if (!ImGui::BeginItemTooltip())
    {
        return;
    }

    const Event &ev = cat.events[id];
    ImGui::Text("ev%d", id);
    if (ev.chapter >= 0)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", MioPanEvents::ChapterLabel(ev.chapter));
    }
    if (ev.load_event)
    {
        ImGui::TextDisabled("Loads its chapter's start room.");
    }
    else if (ev.intro_leaf)
    {
        ImGui::TextDisabled("Chapter set-up: runs as soon as the chapter loads.");
    }

    ImGui::SeparatorText("Opens on");
    ImGui::TextUnformatted(
        MioPanEvents::DescribeConditions(id, EV_TBL_OPEN_COND).c_str());
    const std::string close =
        MioPanEvents::DescribeConditions(id, EV_TBL_CLOSE_COND);
    if (close.find("nothing") != 0)
    {
        ImGui::SeparatorText("Closes on");
        ImGui::TextUnformatted(close.c_str());
    }
    ImGui::SeparatorText("Programs");
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
    ImGui::TextUnformatted(MioPanEvents::DescribePrograms(id).c_str());
    ImGui::PopTextWrapPos();
    if (!ev.place.Known() && ev.go.Known())
    {
        ImGui::Separator();
        ImGui::TextDisabled("No place of its own; Go takes Mio to %s.",
                            MioPanEvents::DescribePlace(ev.go).c_str());
    }
    ImGui::EndTooltip();
}

void EventContextMenu(int id)
{
    if (!ImGui::BeginPopupContextItem())
    {
        return;
    }

    const int state = MioPanEvents::StateOf(id);
    const bool in_game = state >= 0;
    const bool running = state == EV_STATE_INIT || state == EV_STATE_EXE ||
                         state == EV_STATE_WAIT_END;

    ImGui::TextDisabled("ev%d%s%s", id, in_game ? " -- " : "",
                        in_game ? MioPanEvents::StateName(state) : "");
    ImGui::Separator();
    if (ImGui::MenuItem("Start now", nullptr, false,
                        state == EV_STATE_WAIT_OPEN || state == EV_STATE_END))
    {
        MioPanEvents::ForceStart(id);
    }
    Tooltip("Run it without its open conditions -- the ROM's forced start,\n"
            "applied at the top of the next frame's event pass.  Things it\n"
            "expects (a room, a ghost, Mayu) are not set up for it.");
    if (ImGui::MenuItem("Mark as ended", nullptr, false,
                        in_game && !running && state != EV_STATE_END &&
                            state != EV_STATE_LOCK))
    {
        MioPanEvents::ForceEnd(id);
    }
    Tooltip("Set it to ended without running it, so whatever waits on it\n"
            "can go ahead.");
    if (ImGui::MenuItem("Waiting again", nullptr, false,
                        state == EV_STATE_END))
    {
        MioPanEvents::Rearm(id);
    }
    Tooltip("Put an ended event back to waiting for its open conditions,\n"
            "so it can happen again.");
    ImGui::Separator();
    if (ImGui::MenuItem("Copy id"))
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%d", id);
        ImGui::SetClipboardText(text);
    }
    ImGui::EndPopup();
}

/* Columns 1..4 of an event's row. */
void EventCells(const Rows &rows, int id)
{
    const Event &ev = rows.cat.events[id];

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(ev.trigger.c_str());
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(ev.content.c_str());

    ImGui::TableSetColumnIndex(3);
    const int state = MioPanEvents::StateOf(id);
    if (state >= 0)
    {
        const bool armed = MioPanEvents::IsArmed(id);
        ImGui::TextColored(StateColour(state, armed), "%s",
                           armed ? "armed" : MioPanEvents::StateName(state));
    }

    ImGui::TableSetColumnIndex(4);
    ImGui::PushID(id);
    if (ev.chapter >= 0)
    {
        ImGui::BeginDisabled(!rows.can_jump);
        if (ImGui::SmallButton("Jump"))
        {
            MioPanEvents::JumpToEvent(id);
        }
        ImGui::EndDisabled();
        Tooltip(rows.can_jump
                    ? "Restart this chapter, settle everything this event\n"
                      "waits on, and take Mio to where it starts."
                    : rows.jump_why);
        ImGui::SameLine();
    }

    const bool can_go = rows.can_go && ev.go.Known();
    ImGui::BeginDisabled(!can_go);
    if (ImGui::SmallButton("Go"))
    {
        MioPanEvents::TeleportToEvent(id);
    }
    ImGui::EndDisabled();
    if (!ev.go.Known())
    {
        Tooltip("Its conditions name no place to go to.");
    }
    else if (!rows.can_go)
    {
        Tooltip(rows.go_why);
    }
    else
    {
        const std::string text =
            "Take Mio to " + MioPanEvents::DescribePlace(ev.go) +
            " in the game as it stands.";
        Tooltip(text.c_str());
    }
    ImGui::PopID();
}

/* One row, as a tree node; recurses into its sub-events. */
void EventNode(const Rows &rows, int id, const std::string &label)
{
    const Event &ev = rows.cat.events[id];
    if (g_hide_flags && IsFlag(ev))
    {
        return;
    }

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    const bool leaf = ev.subs.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (leaf)
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (id == g_selected)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool open =
        ImGui::TreeNodeEx((void *)(intptr_t)id, flags, "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        g_selected = id;
    }
    EventTooltip(rows.cat, id);
    EventContextMenu(id);
    EventCells(rows, id);

    if (open && !leaf)
    {
        for (int sub : ev.subs)
        {
            const Event &child = rows.cat.events[sub];
            char text[48];
            if (child.place.label < 0 && child.place.room >= 0 && !child.subs.empty())
            {
                std::snprintf(text, sizeof(text), "Room %d  ev%d",
                              child.place.room, sub);
            }
            else
            {
                std::snprintf(text, sizeof(text), "ev%d", sub);
            }
            EventNode(rows, sub, text);
        }
        ImGui::TreePop();
    }
}

/* Under a filter: every match below `root`, flat, in tree order. */
void MatchRows(const Rows &rows, int root)
{
    const Event &ev = rows.cat.events[root];
    if (ev.search.find(rows.filter) != std::string::npos &&
        !(g_hide_flags && IsFlag(ev)))
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                                   ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (root == g_selected)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        ImGui::TreeNodeEx((void *)(intptr_t)root, flags, "ev%d", root);
        if (ImGui::IsItemClicked())
        {
            g_selected = root;
        }
        EventTooltip(rows.cat, root);
        EventContextMenu(root);
        EventCells(rows, root);
    }
    for (int sub : ev.subs)
    {
        MatchRows(rows, sub);
    }
}

int CountMatches(const Rows &rows, int root)
{
    const Event &ev = rows.cat.events[root];
    int count = ev.search.find(rows.filter) != std::string::npos &&
                        !(g_hide_flags && IsFlag(ev))
                    ? 1
                    : 0;
    for (int sub : ev.subs)
    {
        count += CountMatches(rows, sub);
    }
    return count;
}

bool BeginEventTable(const char *id)
{
    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable(id, 5, flags))
    {
        return false;
    }

    const float em = ImGui::GetFontSize();
    ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn("Starts on", ImGuiTableColumnFlags_WidthStretch, 2.4f);
    ImGui::TableSetupColumn("Does", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, em * 4.5f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, em * 5.5f);
    ImGui::TableHeadersRow();
    return true;
}

/* A chapter's children: the load event, then its parts, then whatever else
 * hangs off the chapter event directly. */
void ChapterRows(const Rows &rows, int chapter_event)
{
    int part = 0;
    for (int sub : rows.cat.events[chapter_event].subs)
    {
        const Event &ev = rows.cat.events[sub];
        char label[48];
        if (ev.load_event)
        {
            std::snprintf(label, sizeof(label), "Load  ev%d", sub);
        }
        else if (!ev.subs.empty())
        {
            std::snprintf(label, sizeof(label), "Part %d  ev%d", ++part, sub);
        }
        else
        {
            std::snprintf(label, sizeof(label), "ev%d", sub);
        }
        EventNode(rows, sub, label);
    }
}

void DrawStoryTab(const Rows &rows)
{
    const int playing = MioPanEvents::CurrentChapter();

    for (int c = 0; c < MioPanEvents::kChapterCount; c++)
    {
        const int chapter_event = rows.cat.chapter_event[c];
        if (chapter_event < 0)
        {
            continue;
        }

        ImGui::PushID(c);
        if (!rows.filter.empty())
        {
            if (CountMatches(rows, chapter_event) != 0)
            {
                ImGui::SeparatorText(MioPanEvents::ChapterLabel(c));
                if (BeginEventTable("##matches"))
                {
                    MatchRows(rows, chapter_event);
                    ImGui::EndTable();
                }
            }
            ImGui::PopID();
            continue;
        }

        char header[64];
        std::snprintf(header, sizeof(header), "%s%s###chapter",
                      MioPanEvents::ChapterLabel(c),
                      c == playing ? "   (playing)" : "");
        if (ImGui::CollapsingHeader(header))
        {
            ImGui::BeginDisabled(!rows.can_jump);
            if (ImGui::Button("Start from the beginning"))
            {
                MioPanEvents::StartChapter(c);
            }
            ImGui::EndDisabled();
            Tooltip(rows.can_jump
                        ? "What the ROM's chapter select does: a fresh game\n"
                          "at this chapter's start.  From a game in play the\n"
                          "difficulty, clear count and costume are kept."
                        : rows.jump_why);
            ImGui::SameLine();
            ImGui::TextDisabled("ev%d", chapter_event);

            if (BeginEventTable("##chapter"))
            {
                ChapterRows(rows, chapter_event);
                ImGui::EndTable();
            }
        }
        ImGui::PopID();
    }
}

void DrawTreeTab(const Rows &rows, int root, const char *table_id)
{
    if (root < 0)
    {
        return;
    }
    if (BeginEventTable(table_id))
    {
        if (rows.filter.empty())
        {
            for (int sub : rows.cat.events[root].subs)
            {
                char label[48];
                std::snprintf(label, sizeof(label), "ev%d", sub);
                EventNode(rows, sub, label);
            }
        }
        else
        {
            MatchRows(rows, root);
        }
        ImGui::EndTable();
    }
}

/* Before the event pak is resident only chapters can be started -- the list
 * of what is in them comes out of the pak. */
void DrawWithoutCatalogue(bool can_start, const char *why)
{
    ImGui::TextWrapped(
        "The event list is read out of the game's event data, which is "
        "loaded with a chapter or a save.");
    if (MioPanEvents::CanRequestCatalogue())
    {
        if (ImGui::Button("Load the event list"))
        {
            MioPanEvents::RequestCatalogue();
        }
        Tooltip("Loads the event data the way the ROM's chapter select does.");
    }
    ImGui::Separator();

    for (int c = 0; c < MioPanEvents::kChapterCount; c++)
    {
        ImGui::PushID(c);
        ImGui::BeginDisabled(!can_start);
        if (ImGui::Button(MioPanEvents::ChapterLabel(c)))
        {
            MioPanEvents::StartChapter(c);
        }
        ImGui::EndDisabled();
        Tooltip(can_start ? "Start this chapter from the beginning." : why);
        ImGui::PopID();
        if ((c + 1) % 4 != 0 && c + 1 < MioPanEvents::kChapterCount)
        {
            ImGui::SameLine();
        }
    }
}

void DrawHeader()
{
    ImGui::TextWrapped("%s", MioPanEvents::Status().c_str());
    if (MioPanEvents::Busy())
    {
        if (MioPanEvents::CanProceed())
        {
            if (ImGui::SmallButton("Teleport anyway"))
            {
                MioPanEvents::Proceed();
            }
            Tooltip("Do not wait for the event program that is running.  The\n"
                    "room reload tears it down mid-way, so expect it to\n"
                    "misbehave.");
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("Cancel"))
        {
            MioPanEvents::Cancel();
        }
    }

    const MioPanEvents::Place here = MioPanEvents::PlayerPlace();
    if (here.Known())
    {
        ImGui::TextDisabled("Playing %s, room %d, floor %d",
                            MioPanEvents::ChapterLabel(
                                MioPanEvents::CurrentChapter()),
                            here.room, here.floor);
    }
    else
    {
        ImGui::TextDisabled("Not in a story game.");
    }
}

} // namespace

namespace MioPanUi
{

void DrawEventsMenuItem()
{
    ImGui::MenuItem("Event browser", nullptr, &g_show_events);
    Tooltip("Every event in the game, chapter by chapter: jump to one, take\n"
            "Mio to where it starts, or force its state.");
}

void DrawEventsWindow()
{
    MioPanEvents::Step();

    if (!g_show_events)
    {
        return;
    }

    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 60.0f, em * 40.0f),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("MioPan Events", &g_show_events))
    {
        ImGui::End();
        return;
    }

    DrawHeader();

    const char *jump_why = nullptr;
    const char *go_why = nullptr;
    const bool can_jump = MioPanEvents::CanStartChapter(&jump_why);
    const bool can_go = MioPanEvents::CanTeleport(&go_why);

    ImGui::SetNextItemWidth(em * 8.0f);
    int difficulty = MioPanEvents::TitleDifficulty();
    if (ImGui::Combo("Difficulty", &difficulty,
                     "Easy\0Normal\0Hard\0Nightmare\0"))
    {
        MioPanEvents::SetTitleDifficulty(difficulty);
    }
    Tooltip("For a chapter started from the title screen.  One started from\n"
            "a game in play keeps that game's difficulty.");

    const Catalogue *cat = MioPanEvents::GetCatalogue();
    if (cat == nullptr)
    {
        ImGui::Separator();
        DrawWithoutCatalogue(can_jump, jump_why);
        ImGui::End();
        return;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 14.0f);
    ImGui::InputTextWithHint("##filter", "Filter: ev1563, movie, room 65...",
                             g_filter, sizeof(g_filter));
    ImGui::SameLine();
    ImGui::Checkbox("Hide flags", &g_hide_flags);
    Tooltip("Hide events with no program and no sub-events: markers other\n"
            "events test, with nothing to see.");

    const Rows rows = {*cat, can_jump, jump_why, can_go, go_why,
                       Lower(g_filter)};

    if (ImGui::BeginTabBar("##events"))
    {
        if (ImGui::BeginTabItem("Story"))
        {
            ImGui::BeginChild("##story");
            DrawStoryTab(rows);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Rooms"))
        {
            ImGui::TextDisabled("Examine points, doors and pick-ups. They run in "
                                "every chapter; their conditions say which.");
            ImGui::BeginChild("##rooms");
            DrawTreeTab(rows, cat->room_root, "##room_events");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Shared"))
        {
            ImGui::BeginChild("##shared");
            DrawTreeTab(rows, cat->system_root, "##shared_events");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace MioPanUi
