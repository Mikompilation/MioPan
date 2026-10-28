#include "miopan_rml_saves.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rmlui.h"
#include "../miopan_memory.h"

#include "../../graphics/graph2d/tim2.h"
#include "../../ingame/menu/plyr_room_info.h"
#include "../../system/mc/prg/mc_set_data.h"

#include <RmlUi/Core.h>

#include <cstdio>
#include <vector>

namespace
{
/* Memory-card messages, type 0x50: the line a cleared save shows in place of
 * its chapter. */
constexpr int kMsgType = 0x50;
constexpr int kMsgClearData = 0x32;

/* The part of a room's snapshot pak the game shows: save_load_tex[59]. */
constexpr int kPictureU = 1;
constexpr int kPictureV = 1;
constexpr int kPictureW = 105;
constexpr int kPictureH = 62;

void SetText(Rml::Element *element, const std::string &text)
{
    if (element != nullptr)
    {
        element->SetInnerRML(MioPanRmlCommon::Escape(text));
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

namespace MioPanRmlSaves
{
bool SaveRow::Attach(Rml::ElementDocument *document)
{
    Detach();
    if (document == nullptr)
    {
        return false;
    }
    slots_ = document->GetElementById("slots");
    detail_ = document->GetElementById("detail");
    chapter_ = document->GetElementById("detail-chapter");
    room_ = document->GetElementById("detail-room");
    clears_ = document->GetElementById("detail-clears");
    time_ = document->GetElementById("detail-time");
    if (slots_ == nullptr || detail_ == nullptr)
    {
        Detach();
        return false;
    }

    std::string rml;
    for (int i = 0; i < kSlots; i++)
    {
        rml += "<div id=\"slot-" + std::to_string(i) + "\" class=\"slot\"></div>";
    }
    slots_->SetInnerRML(rml);
    for (int i = 0; i < kSlots; i++)
    {
        cards_[i].element = document->GetElementById("slot-" + std::to_string(i));
    }
    return true;
}

void SaveRow::Detach()
{
    *this = SaveRow();
}

bool SaveRow::HasData(int slot) const
{
    return list_ && slot >= 0 && slot < kSlots &&
           GetMemoryCardPlayDataFlg(slot) == 1;
}

bool SaveRow::IsOpen(int slot) const
{
    return list_ && slot >= 0 && slot < kSlots && (open_ >> slot & 1) != 0;
}

int SaveRow::Step(int direction) const
{
    for (int step = 1; step < kSlots; step++)
    {
        const int slot = (cursor_ + direction * step + kSlots * kSlots) % kSlots;
        if (IsOpen(slot))
        {
            return slot;
        }
    }
    return -1;
}

int SaveRow::SlotAt(Rml::Element *element) const
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        for (int i = 0; i < kSlots; i++)
        {
            if (element == cards_[i].element)
            {
                return i;
            }
        }
    }
    return -1;
}

void SaveRow::Refresh(bool list, unsigned open)
{
    if (slots_ == nullptr)
    {
        return;
    }
    list_ = list;
    open_ = open;
    for (int i = 0; i < kSlots; i++)
    {
        Card &card = cards_[i];
        const bool has = HasData(i);
        std::string rml;
        if (has && !card.picture.empty())
        {
            rml = "<img class=\"picture\" src=\"" + card.picture + "\"/>";
        }
        else
        {
            rml = std::string("<div class=\"blank\">") +
                  (has ? "" : "No Data") + "</div>";
        }
        rml += "<div class=\"number\">No. " + std::to_string(i + 1) + "</div>";
        if (card.element == nullptr)
        {
            continue;
        }
        if (rml != card.built)
        {
            card.element->SetInnerRML(rml);
            card.built = rml;
        }
        card.element->SetClass("empty", !has);
        card.element->SetClass("locked", !IsOpen(i));
    }
    slots_->SetClass("hidden", !list_);
    detail_->SetClass("hidden", !list_);
}

void SaveRow::Select(int slot)
{
    cursor_ = slot;
    for (int i = 0; i < kSlots; i++)
    {
        if (cards_[i].element != nullptr)
        {
            cards_[i].element->SetClass("selected", list_ && i == slot);
        }
    }
    RefreshDetail();
}

/* What the chosen save holds, under the row. */
void SaveRow::RefreshDetail()
{
    const int slot = cursor_;
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
    if (key == shown_detail_)
    {
        return;
    }
    shown_detail_ = key;
    SetText(chapter_, chapter);
    SetText(room_, room);
    SetText(clears_, clears);
    SetText(time_, time);
}

void SaveRow::SetPicture(int slot, const void *pk2, int label)
{
    if (slot < 0 || slot >= kSlots)
    {
        return;
    }
    std::string &picture = cards_[slot].picture;
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

void SaveRow::Reset()
{
    for (Card &card : cards_)
    {
        card.picture.clear();
    }
    shown_detail_.clear();
}
}
