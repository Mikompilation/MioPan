#ifndef MIOPAN_RML_SAVES_H
#define MIOPAN_RML_SAVES_H

#include <string>

namespace Rml
{
class Element;
class ElementDocument;
}

/*
 * The memory card's five saves as the load and save screens show them: a row
 * of cards, each with its room's picture, and beneath it what the chosen one
 * holds -- chapter, room, clears, play time.  The markup lives in each
 * screen's document, the look in load.rcss:
 *
 *   <div id="slots"></div>
 *   <div id="detail">
 *     <p id="detail-chapter"></p><p id="detail-room"></p>
 *     <div id="detail-foot">
 *       <span id="detail-clears"></span><span id="detail-time"></span>
 *     </div>
 *   </div>
 *
 * What a slot holds is read from the card's header as the game last loaded or
 * wrote it (mc_set_data.h).
 */
namespace MioPanRmlSaves
{
constexpr int kSlots = 5;

class SaveRow
{
public:
    /* Builds the five cards inside #slots. */
    bool Attach(Rml::ElementDocument *document);
    void Detach();

    /* Whether the saves are known yet -- the row and the detail stay hidden
     * until they are -- and which slots can take the cursor, a bit each. */
    void Refresh(bool list, unsigned open);
    void Select(int slot);
    int Cursor() const { return cursor_; }

    bool HasData(int slot) const;
    bool IsOpen(int slot) const;
    /* The next slot that way that can take the cursor, going round; -1 when
     * there is none but the one it is on. */
    int Step(int direction) const;
    /* The slot `element` is in, or -1. */
    int SlotAt(Rml::Element *element) const;

    /* Slot `slot`'s room picture: the snapshot pak the game read for it (file
     * `label`), or null when it has none. */
    void SetPicture(int slot, const void *pk2, int label);
    /* Forget the pictures and what was last shown, for the next visit. */
    void Reset();

private:
    struct Card
    {
        Rml::Element *element = nullptr;
        /* What the card was last built from, so it is rebuilt only when that
         * changes. */
        std::string built;
        std::string picture;    /* a generated image's name, or "" */
    };

    void RefreshDetail();

    Rml::Element *slots_ = nullptr;
    Rml::Element *detail_ = nullptr;
    Rml::Element *chapter_ = nullptr;
    Rml::Element *room_ = nullptr;
    Rml::Element *clears_ = nullptr;
    Rml::Element *time_ = nullptr;
    Card cards_[kSlots];
    std::string shown_detail_;
    bool list_ = false;
    unsigned open_ = 0;
    int cursor_ = 0;
};
}

#endif
