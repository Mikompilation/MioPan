/* ==========================================================================
 *  ingame/item/dat/item_dat.c
 *
 *  The master item table.  Pure data file: item_dat.o contributes no code of
 *  its own -- its whole .text is fixed_array template boilerplate pulled in by
 *  the header.
 *
 *  Read straight out of SLES_523.84 at data 0x318930, 58 entries of 0x2c
 *  bytes.  Entries 0..4 are camera film, which is why ItemFilmEquip() can
 *  drop item_id into a CVariable<char,0,4> without translating it.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "item_dat.h"

/* item_id, type, get_max, def_use_num, value, item_name */
ITEM_DAT item_dat[ITEM_DAT_MAX] =                                   /* data 318930 */
{
    {  0, ITEM_TYPE_FILM,    99, 1,  10, "CAM_FILM_07" },
    {  1, ITEM_TYPE_FILM,    99, 1,  15, "CAM_FILM_14" },
    {  2, ITEM_TYPE_FILM,    99, 1,  17, "CAM_FILM_61" },
    {  3, ITEM_TYPE_FILM,    99, 1,  25, "CAM_FILM_90" },
    {  4, ITEM_TYPE_FILM,    99, 1,  30, "CAM_FILM_00" },
    {  5, ITEM_TYPE_HP,      99, 1,  33, "ITM_MANYOUGAN" },
    {  6, ITEM_TYPE_HP,      99, 1, 100, "ITM_GOSINSUI" },
    {  7, ITEM_TYPE_NONE,     1, 1,   0, "ITM_KAGAMIISI" },
    {  8, ITEM_TYPE_SP,      99, 1,  33, "ITM_REISEKI" },
    {  9, ITEM_TYPE_CONSUME, 99, 1,  60, "ITM_GEKKAKOU" },
    { 10, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_CAMERA" },
    { 11, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_LIGHT" },
    { 12, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_HUTAGO_KEY1" },
    { 13, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_HUTAGO_KEY2" },
    { 14, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_MIYAKO_BOOK1" },
    { 15, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_MIYAKO_BOOK2" },
    { 16, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_KOMONJO" },
    { 17, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_HUTAGO_PHOTO" },
    { 18, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_OUSAKA_MAP" },
    { 19, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_HINAKUBI" },
    { 20, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_HUDAKAGI_HIGASI" },
    { 21, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_HUDAKAGI_HINA" },
    { 22, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_HUDAKAGI_DOZOU" },
    { 23, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_HUDAKAGI_WAKIDO" },
    { 24, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_KYAKUMA_KEY" },
    { 25, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_PZL_ROKU_HON1" },
    { 26, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_PZL_ROKU_HON2" },
    { 27, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_PZL_ROKU_HON3" },
    { 28, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_PZL_ROKU_HON4" },
    { 29, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_PZL_ROKU_HON5" },
    { 30, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_ZASHIKI_NAI_KEY1" },
    { 31, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_ZASHIKI_NAI_KEY2" },
    { 32, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_ZASHIKI_GAI_KEY" },
    { 33, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_MARI" },
    { 34, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_REEL1" },
    { 35, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_REEL2" },
    { 36, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_REEL3" },
    { 37, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_REEL4" },
    { 38, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_REEL5" },
    { 39, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_REEL6" },
    { 40, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_REEL7" },
    { 41, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_SUZU" },
    { 42, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_DOLL_HEAD" },
    { 43, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_DOLL_R_ARM" },
    { 44, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_DOLL_L_ARM" },
    { 45, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_DOLL_EYE" },
    { 46, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_KAZA_PANEL1" },
    { 47, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_KAZA_PANEL2" },
    { 48, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_KAZA_PANEL3" },
    { 49, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_KAZA_PANEL4" },
    { 50, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_RADIO" },
    { 51, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_MIYAKO_BAG" },
    { 52, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_DOLL_SEKKEI" },
    { 53, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_HIMO_R_DOLL" },
    { 54, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_NARABI_KEY" },
    { 55, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_KURA_KEY" },
    { 56, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_FUKAMICHI_KEY" },
    { 57, ITEM_TYPE_EVENT,    1, 1,   0, "ITM_RING" },
};
