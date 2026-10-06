/* ==========================================================================
 *  ingame/plyr/ChrSort.h
 *
 *  Character registration in MapPut's depth-sorted draw list.
 * ======================================================================== */

#ifndef _INGAME_CHRSORT_H
#define _INGAME_CHRSORT_H

struct ENE_WRK;
struct FLY_WRK;
struct HeaderSection;

typedef float CHR_SORT_MATRIX[4][4];

void ChrSortSetFlg(int flg);
void ChrSortDelFlg(int flg);
void ChrSortEnemCallback(void);
void ChrSortFlyCallback(void);
int ChrSortRegistEnem(ENE_WRK *ene);
int ChrSortRegistFly(FLY_WRK *fly);
int ChrSortDeleteEnem(ENE_WRK *ene);
int ChrSortDeleteFly(FLY_WRK *fly);
CHR_SORT_MATRIX *ChrSortGetSgdMatrix(HeaderSection *hs);
CHR_SORT_MATRIX *ChrSortGetPlayrMatrix(void);
CHR_SORT_MATRIX *ChrSortGetSisMatrix(void);
int ChrSortRegistSis(void);
int ChrSortRegistPlayr(void);
int ChrSortDelete(int id);
void ChrSortInit(void);

#endif /* _INGAME_CHRSORT_H */
