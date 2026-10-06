/* ==========================================================================
 *  system/mc/dat/mc_title_dat.c
 *
 *  Memory-card naming tables, read straight out of the Feb 6 2004 prototype
 *  (SLES_523.84): .data 0x31e3c8, .sdata 0x3f1b40 and .rodata 0x3bbc70.
 *  mc_title_dat.o has no .text at all.
 *
 *  The Japanese browser titles are the ROM's own Shift-JIS bytes, written as
 *  hex escapes so the file stays ASCII; the English gloss is in the comment.
 *  They are full-width Latin, not kana, which is why the byte pairs are all
 *  0x81/0x82-led.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_title_dat.h"

/* --------------------------------------------------------------------------
 *  Directory names.  "BE" is the PAL region prefix, "SLES-52384" the disc's
 *  own product code, so these are exactly the strings the PS2 browser lists.
 *  Note that game_dir_name[0] doubles as game_file_name[1] below.
 * ------------------------------------------------------------------------ */
char *game_dir_name[6] =                        /* data 31e3c8 */
{
    (char *)"BESLES-52384ZERO-BD",              /* 0  game data             */
    (char *)"BESLES-52384ZERO-BA1",             /* 1  photo album 1         */
    (char *)"BESLES-52384ZERO-BA2",             /* 2  photo album 2         */
    (char *)"BESLES-52384ZERO-BA3",             /* 3  photo album 3         */
    (char *)"BESLES-52384ZERO-BA4",             /* 4  photo album 4         */
    (char *)"BESLES-52384ZERO-BA5"              /* 5  photo album 5         */
};

/* Files inside the game-data directory.  [1] is the play-data header, and the
 * ROM points it at the same literal as game_dir_name[0] -- the file named
 * after its own directory is the one the browser treats as the save's owner. */
char *game_file_name[7] =                       /* data 31e3e0 */
{
    (char *)"Zero2System",                      /* 0  options + clear flags */
    (char *)"BESLES-52384ZERO-BD",              /* 1  play-data header      */
    (char *)"Zero2Play0",                       /* 2  save slot 1           */
    (char *)"Zero2Play1",                       /* 3  save slot 2           */
    (char *)"Zero2Play2",                       /* 4  save slot 3           */
    (char *)"Zero2Play3",                       /* 5  save slot 4           */
    (char *)"Zero2Play4"                        /* 6  save slot 5           */
};

/* One file per album directory, and the file is named after the directory for
 * the same reason game_file_name[1] is.  Five separate one-entry tables rather
 * than a [5][1] because MemoryCardSetFilePath() reaches them through a switch.
 *
 * On the EE these sit eight bytes apart in .sdata although each holds a single
 * four-byte pointer; the padding is the target's small-data alignment and has
 * no meaning -- nothing walks past [0]. */
char *album1_file_name[1] = { (char *)"BESLES-52384ZERO-BA1" };  /* sdata 3f1b40 */
char *album2_file_name[1] = { (char *)"BESLES-52384ZERO-BA2" };  /* sdata 3f1b48 */
char *album3_file_name[1] = { (char *)"BESLES-52384ZERO-BA3" };  /* sdata 3f1b50 */
char *album4_file_name[1] = { (char *)"BESLES-52384ZERO-BA4" };  /* sdata 3f1b58 */
char *album5_file_name[1] = { (char *)"BESLES-52384ZERO-BA5" };  /* sdata 3f1b60 */

/* --------------------------------------------------------------------------
 *  Icon file names, [dir_label][icon_type].  icon_type is 0 view / 1 copy /
 *  2 delete, and all three name the same file: the game ships one icon per
 *  directory and lets icon.sys point all three browser states at it.
 * ------------------------------------------------------------------------ */
char mc_icon_name[6][3][31] =                   /* rdata 3bbd60 */
{
    { "zero-b-game.ico",   "zero-b-game.ico",   "zero-b-game.ico"   },  /* 0 */
    { "zero-b-album0.ico", "zero-b-album0.ico", "zero-b-album0.ico" },  /* 1 */
    { "zero-b-album1.ico", "zero-b-album1.ico", "zero-b-album1.ico" },  /* 2 */
    { "zero-b-album2.ico", "zero-b-album2.ico", "zero-b-album2.ico" },  /* 3 */
    { "zero-b-album3.ico", "zero-b-album3.ico", "zero-b-album3.ico" },  /* 4 */
    { "zero-b-album4.ico", "zero-b-album4.ico", "zero-b-album4.ico" }   /* 5 */
};

/* First browser row, full-width Shift-JIS: "Project Zero 2" (the European
 * title; the disc is SLES_523.84).  28 bytes plus the terminator, and
 * icon.sys's OffsLF is 28 -- the break lands exactly at the end of it. */
char mc_icon_title[29] =                        /* rdata 3bbf90 */
    "\x82\x6f\x82\x92\x82\x8f\x82\x8a\x82\x85\x82\x83\x82\x94"   /* Project   */
    "\x81\x40"                                                    /* (space)   */
    "\x82\x79\x82\x85\x82\x92\x82\x8f"                            /* Zero      */
    "\x81\x40"                                                    /* (space)   */
    "\x82\x51";                                                   /* 2         */

/* Second browser row, one per directory. */
char mc_icon_sub_title[6][64] =                 /* rdata 3bbfb0 */
{
    /* "GameData"   */ "\x82\x66\x82\x81\x82\x8d\x82\x85\x82\x63\x82\x81\x82\x94\x82\x81",
    /* "AlbumData1" */ "\x82\x60\x82\x8c\x82\x82\x82\x95\x82\x8d\x82\x63\x82\x81\x82\x94\x82\x81\x82\x50",
    /* "AlbumData2" */ "\x82\x60\x82\x8c\x82\x82\x82\x95\x82\x8d\x82\x63\x82\x81\x82\x94\x82\x81\x82\x51",
    /* "AlbumData3" */ "\x82\x60\x82\x8c\x82\x82\x82\x95\x82\x8d\x82\x63\x82\x81\x82\x94\x82\x81\x82\x52",
    /* "AlbumData4" */ "\x82\x60\x82\x8c\x82\x82\x82\x95\x82\x8d\x82\x63\x82\x81\x82\x94\x82\x81\x82\x53",
    /* "AlbumData5" */ "\x82\x60\x82\x8c\x82\x82\x82\x95\x82\x8d\x82\x63\x82\x81\x82\x94\x82\x81\x82\x54"
};

/* CD file numbers of the icon models, [dir_label][icon_type].  Six consecutive
 * files -- one per directory, shared across the three icon states, matching
 * mc_icon_name[] above. */
int icon_data_label[6][3] =                     /* rdata 3bc130 */
{
    { 4477, 4477, 4477 },                       /* 0  game data             */
    { 4478, 4478, 4478 },                       /* 1  photo album 1         */
    { 4479, 4479, 4479 },                       /* 2  photo album 2         */
    { 4480, 4480, 4480 },                       /* 3  photo album 3         */
    { 4481, 4481, 4481 },                       /* 4  photo album 4         */
    { 4482, 4482, 4482 }                        /* 5  photo album 5         */
};
