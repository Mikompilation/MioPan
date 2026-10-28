#include "miopan_rml_load.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rmlui.h"
#include "../miopan_memory.h"

#include "../../graphics/graph2d/tim2.h"
#include "../../ingame/menu/plyr_room_info.h"
#include "../../system/mc/prg/mc_set_data.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/load.rml";

constexpr int kSlots = 5;

/* Memory-card messages, type 0x50: "load this?", and the line a cleared save
 * shows in place of its chapter. */
constexpr int kMsgType = 0x50;
constexpr int kMsgLoadThis = 0x0e;
constexpr int kMsgClearData = 0x32;

/* The part of a room's snapshot pak the game shows: save_load_tex[59]. */
constexpr int kPictureU = 1;
constexpr int kPictureV = 1;
constexpr int kPictureW = 105;
constexpr int kPictureH = 62;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct Slot
{
    Rml::Element *element = nullptr;
    /* What the element was last built from, so it is rebuilt only when that
     * changes. */
    std::string built;
    std::string picture;    /* a generated image's name, or "" */
};

struct LoadState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *slots_box = nullptr;
    Rml::Element *detail = nullptr;
    Rml::Element *chapter = nullptr;
    Rml::Element *room = nullptr;
    Rml::Element *clears = nullptr;
    Rml::Element *time = nullptr;
    Rml::Element *message = nullptr;
    Rml::Element *prompts = nullptr;
    Slot slots[kSlots];
    std::string shown_prompts;
    std::string shown_detail;
    std::string shown_message;

    int mode = -1;
    bool list = false;
    int cursor = 0;

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_LOAD_NONE;
    MioPanRmlCommon::ConfirmDialog confirm;
};

LoadState g_load;
Listener g_listener;

bool HasData(int slot)
{
    return g_load.list && GetMemoryCardPlayDataFlg(slot) == 1;
}

void SetText(Rml::Element *element, const std::string &text)
{
    if (element != nullptr)
    {
        element->SetInnerRML(MioPanRmlCommon::Escape(text));
    }
}

/* What the chosen save holds, under the row. */
void RefreshDetail()
{
    const int slot = g_load.cursor;
    std::string chapter;
    std::string room;
    std::string clears;
    std::string time;
    if (HasData(slot))
    {
        if (GetMemoryCardClearDataFlg(slot) != 0)
        {
            chapter = MioPanGameText::Message(kMsgType, kMsgClearData);
        }
        else
        {
            /* The same bounds SaveLoadMcPlayDataInfoDisp() keeps. */
            const int chapter_no = GetMemoryCardPlayDataChapter(slot);
            const int room_no = GetMemoryCardPlayDataRoomLabel(slot);
            if ((unsigned)chapter_no < 11)
            {
                chapter = MioPanGameText::Message(3, chapter_no);
            }
            if ((unsigned)room_no < 240)
            {
                room = MioPanGameText::Message(0x51,
                                               GetMapLabelToRoomLabel(room_no));
            }
        }
        const int clear_num = GetMemoryCardPlayDataClearNum(slot);
        if (clear_num == 1)
        {
            clears = "Cleared once";
        }
        else if (clear_num > 1)
        {
            clears = "Cleared " + std::to_string(clear_num) + " times";
        }
        const TIME_INFO played = GetMemoryCardPlayDataPlayTime(slot);
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "Play time  %03d:%02d:%02d",
                      played.hour, played.min, played.sec);
        time = buffer;
    }

    const std::string key = chapter + '\n' + room + '\n' + clears + '\n' + time;
    if (key == g_load.shown_detail)
    {
        return;
    }
    g_load.shown_detail = key;
    SetText(g_load.chapter, chapter);
    SetText(g_load.room, room);
    SetText(g_load.clears, clears);
    SetText(g_load.time, time);
}

void Select(int slot)
{
    g_load.cursor = slot;
    for (int i = 0; i < kSlots; i++)
    {
        if (g_load.slots[i].element != nullptr)
        {
            g_load.slots[i].element->SetClass("selected",
                                              g_load.list && i == slot);
        }
    }
    RefreshDetail();
}

void RefreshSlots()
{
    for (int i = 0; i < kSlots; i++)
    {
        Slot &slot = g_load.slots[i];
        const bool has = HasData(i);
        std::string rml;
        if (has && !slot.picture.empty())
        {
            rml = "<img class=\"picture\" src=\"" + slot.picture + "\"/>";
        }
        else
        {
            rml = std::string("<div class=\"blank\">") +
                  (has ? "" : "No Data") + "</div>";
        }
        rml += "<div class=\"number\">No. " + std::to_string(i + 1) + "</div>";
        if (rml != slot.built && slot.element != nullptr)
        {
            slot.element->SetInnerRML(rml);
            slot.element->SetClass("empty", !has);
            slot.built = rml;
        }
    }
    g_load.slots_box->SetClass("hidden", !g_load.list);
    g_load.detail->SetClass("hidden", !g_load.list);
}

void Queue(int command)
{
    g_load.command = command;
}

/* The next save with data that way, as the game's cursor steps; it stays put
 * when there is none. */
void Move(int direction)
{
    for (int step = 1; step < kSlots; step++)
    {
        const int slot = (g_load.cursor + direction * step + kSlots * kSlots) % kSlots;
        if (HasData(slot))
        {
            Select(slot);
            MioPanRmlCommon::PlayCursor();
            return;
        }
    }
}

void Choose(int slot)
{
    if (!HasData(slot))
    {
        return;
    }
    Select(slot);
    MioPanRmlCommon::PlayDecide();
    std::string question = MioPanRmlCommon::NameButtonIcons(
        MioPanGameText::Message(kMsgType, kMsgLoadThis));
    if (question.empty())
    {
        question = "Load this save?";
    }
    /* The phase loads it; the window has played the decide cue. */
    g_load.confirm.Open(question, "", [] { Queue(MIOPAN_RML_LOAD_CHOSEN); });
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    if (g_load.mode == MIOPAN_RML_LOAD_NOTICE)
    {
        switch (key)
        {
        case Rml::Input::KI_RETURN:
        case Rml::Input::KI_NUMPADENTER:
        case Rml::Input::KI_SPACE:
        case Rml::Input::KI_ESCAPE:
            Queue(MIOPAN_RML_LOAD_ACK);
            event.StopPropagation();
            break;
        default:
            break;
        }
        return;
    }
    if (g_load.mode != MIOPAN_RML_LOAD_SELECT)
    {
        return;
    }
    if (g_load.confirm.HandleKey(key))
    {
        event.StopPropagation();
        return;
    }
    switch (key)
    {
    case Rml::Input::KI_LEFT:
    case Rml::Input::KI_UP: Move(-1); break;
    case Rml::Input::KI_RIGHT:
    case Rml::Input::KI_DOWN: Move(1); break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE: Choose(g_load.cursor); break;
    /* The phase plays the cancel cue as it goes back. */
    case Rml::Input::KI_ESCAPE: Queue(MIOPAN_RML_LOAD_BACK); break;
    default: return;
    }
    event.StopPropagation();
}

int SlotAt(Rml::Element *element)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        for (int i = 0; i < kSlots; i++)
        {
            if (element == g_load.slots[i].element)
            {
                return i;
            }
        }
    }
    return -1;
}

bool InElement(Rml::Element *element, const char *id)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        if (element->GetId() == id)
        {
            return true;
        }
    }
    return false;
}

void Listener::ProcessEvent(Rml::Event &event)
{
    /* Nothing more until the phase has taken the last answer. */
    if (!g_load.visible || g_load.command != MIOPAN_RML_LOAD_NONE)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }
    if (g_load.mode == MIOPAN_RML_LOAD_NOTICE)
    {
        if (event == Rml::EventId::Click)
        {
            Queue(MIOPAN_RML_LOAD_ACK);
        }
        return;
    }
    if (g_load.mode != MIOPAN_RML_LOAD_SELECT || g_load.confirm.IsOpen())
    {
        return;
    }

    Rml::Element *target = event.GetTargetElement();
    const int slot = SlotAt(target);
    if (event == Rml::EventId::Mouseover)
    {
        if (MioPanRml::PointerActive() && slot >= 0 && slot != g_load.cursor &&
            HasData(slot))
        {
            Select(slot);
            MioPanRmlCommon::PlayCursor();
        }
    }
    else if (event == Rml::EventId::Click)
    {
        if (slot >= 0)
        {
            Choose(slot);
        }
        else if (InElement(target, "prompt-back"))
        {
            Queue(MIOPAN_RML_LOAD_BACK);
        }
    }
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kSelect[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    static const MioPanRmlCommon::Prompt kNotice[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "OK", nullptr},
    };
    std::string prompts;
    if (g_load.mode == MIOPAN_RML_LOAD_SELECT)
    {
        prompts = MioPanRmlCommon::PromptsRml(kSelect, 2);
    }
    else if (g_load.mode == MIOPAN_RML_LOAD_NOTICE)
    {
        prompts = MioPanRmlCommon::PromptsRml(kNotice, 1);
    }
    if (prompts != g_load.shown_prompts && g_load.prompts != nullptr)
    {
        g_load.prompts->SetInnerRML(prompts);
        g_load.shown_prompts = prompts;
    }
}

/* A room's snapshot as RGBA: the pak's first picture, cut to what the game
 * shows of it.  The game's colours run 0..0x80 in alpha. */
bool DecodePicture(const void *pk2, std::vector<Uint8> &rgba)
{
    const auto *base =
        (const unsigned char *)MioPan_GetHostPointer((uintptr_t)pk2);
    if (base == nullptr || *(const int *)base < 1)
    {
        return false;
    }
    void *tim2 = (void *)(base + *(const int *)(base + 0x10));
    TIM2_PICTUREHEADER *picture = Tim2GetPictureHeader(tim2, 0);
    if (picture == nullptr || picture->ImageWidth < kPictureU + kPictureW ||
        picture->ImageHeight < kPictureV + kPictureH)
    {
        return false;
    }
    rgba.resize((size_t)kPictureW * kPictureH * 4);
    for (int y = 0; y < kPictureH; y++)
    {
        for (int x = 0; x < kPictureW; x++)
        {
            const u_int c = Tim2GetTextureColor(picture, 0, 0, kPictureU + x,
                                                kPictureV + y);
            Uint8 *out = &rgba[((size_t)y * kPictureW + x) * 4];
            out[0] = (Uint8)(c & 0xff);
            out[1] = (Uint8)(c >> 8 & 0xff);
            out[2] = (Uint8)(c >> 16 & 0xff);
            const u_int alpha = (c >> 24 & 0xff) * 2;
            out[3] = (Uint8)(alpha > 255 ? 255 : alpha);
        }
    }
    return true;
}
}

namespace MioPanRmlLoad
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_load.document = context->LoadDocument(kDocumentPath);
    if (g_load.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    Rml::ElementDocument *document = g_load.document;
    g_load.slots_box = document->GetElementById("slots");
    g_load.detail = document->GetElementById("detail");
    g_load.chapter = document->GetElementById("detail-chapter");
    g_load.room = document->GetElementById("detail-room");
    g_load.clears = document->GetElementById("detail-clears");
    g_load.time = document->GetElementById("detail-time");
    g_load.message = document->GetElementById("message");
    g_load.prompts = document->GetElementById("prompts");
    if (g_load.slots_box == nullptr || g_load.detail == nullptr ||
        g_load.message == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        document->Close();
        g_load = LoadState();
        return false;
    }

    std::string rml;
    for (int i = 0; i < kSlots; i++)
    {
        rml += "<div id=\"slot-" + std::to_string(i) + "\" class=\"slot\"></div>";
    }
    g_load.slots_box->SetInnerRML(rml);
    for (int i = 0; i < kSlots; i++)
    {
        g_load.slots[i].element =
            document->GetElementById("slot-" + std::to_string(i));
    }

    g_load.confirm.Attach(document, "confirm");
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover})
    {
        document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_load.confirm.Detach();
    g_load = LoadState();
}

void Frame()
{
    if (g_load.visible && !g_load.requested)
    {
        g_load.confirm.Close();
        g_load.document->Hide();
        g_load.visible = false;
        g_load.mode = -1;
        g_load.shown_message.clear();
        g_load.command = MIOPAN_RML_LOAD_NONE;
        for (Slot &slot : g_load.slots)
        {
            slot.picture.clear();
        }
    }
    g_load.shown_last_frame = g_load.visible;
    g_load.requested = false;
    if (g_load.visible)
    {
        RefreshPrompts();
    }
}
}

extern "C" {

int MioPan_RmlLoadShow(int mode, int msg_id, int cursor, int list)
{
    if (g_load.document == nullptr)
    {
        return 0;
    }
    g_load.requested = true;
    if (!g_load.visible)
    {
        g_load.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_load.visible = true;
        g_load.command = MIOPAN_RML_LOAD_NONE;
        g_load.shown_detail.clear();
    }

    const bool was_list = g_load.list;
    g_load.list = list != 0;
    if (mode != g_load.mode)
    {
        /* Choosing starts where loadgame.c put the cursor: on the first save
         * with data. */
        if (mode == MIOPAN_RML_LOAD_SELECT)
        {
            g_load.cursor = cursor >= 0 && cursor < kSlots ? cursor : 0;
        }
        if (mode != MIOPAN_RML_LOAD_SELECT)
        {
            g_load.confirm.Close();
        }
        g_load.mode = mode;
        g_load.message->SetClass("notice", mode == MIOPAN_RML_LOAD_NOTICE);
        RefreshPrompts();
    }
    /* Its button icons follow the pad or keyboard in use. */
    const std::string message = MioPanRmlCommon::NameButtonIcons(
        MioPanGameText::Message(kMsgType, msg_id));
    if (message != g_load.shown_message)
    {
        g_load.shown_message = message;
        SetText(g_load.message, message);
    }

    RefreshSlots();
    if (was_list != g_load.list || g_load.mode == MIOPAN_RML_LOAD_SELECT)
    {
        Select(g_load.cursor);
    }
    else
    {
        RefreshDetail();
    }
    return 1;
}

int MioPan_RmlLoadIsShown(void)
{
    return g_load.shown_last_frame ? 1 : 0;
}

void MioPan_RmlLoadSetPicture(int slot, const void *pk2, int label)
{
    if (slot < 0 || slot >= kSlots)
    {
        return;
    }
    std::string &picture = g_load.slots[slot].picture;
    if (pk2 == nullptr || label < 0)
    {
        picture.clear();
        return;
    }
    /* One room, one picture: the name is the pak's file number. */
    const std::string name =
        std::string(MioPanRml::kGeneratedPrefix) + "snap/" + std::to_string(label);
    if (!MioPanRml::HasGeneratedImage(name))
    {
        std::vector<Uint8> rgba;
        if (!DecodePicture(pk2, rgba))
        {
            picture.clear();
            return;
        }
        MioPanRml::SetGeneratedImage(name, rgba.data(), kPictureW, kPictureH);
    }
    picture = name;
}

int MioPan_RmlLoadConsumeCommand(void)
{
    const int command = g_load.command;
    g_load.command = MIOPAN_RML_LOAD_NONE;
    return command;
}

int MioPan_RmlLoadCursor(void)
{
    return g_load.cursor;
}
}
