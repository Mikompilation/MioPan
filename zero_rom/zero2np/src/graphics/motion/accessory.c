// FILE: /home/zero_rom/zero2np/src/graphics/motion/accessory.c
//
// Accessory simulation: ropes, cloth and the collision volumes they are pushed
// out of.  Ropes and cloth are both mass-spring systems over C_PARTICLE, and
// both are resolved against the same three primitives (sphere, tube, plane).
// "Chodo" is the hanging-cloth variant used for map drapes, which additionally
// carries a WIND_CTRL.
//
// Complete: all 42 symbols ZERO2.MAP exports, plus the two file statics
// motClothSetWindTime and motClothWind.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "accessory.h"

#include "acs_dat.h"                            /* rope_tbl                  */
#include "mdldat.h"                             /* manmdl_dat                */
#include "motion.h"                             /* plyracs_ctrl / rope_ctrl  */
#include "../graph3d/gra3dSGD.h"                /* _gra3dDrawSGD             */
#include "../graph3d/gra3dSGDData.h"            /* sgdCalcBoneCoordinate     */
#include "../graph3d/g3dxVu0.h"                 /* g3dxVu0Sqrt               */
#include "mdlact.h"                             /* motGetRandom              */
#include "../graph3d/ctl/fixed_array.h"
#include "../../common/packfile.h"              /* GetFileInPak              */
#include "../../common/variable.h"              /* sys_wrk                   */
#include "../../common/zero2_util.h"            /* sceVu0* helpers           */

#include <stdio.h>
#include <stdlib.h>                             /* rand                      */
#include <string.h>

#define ROPE_CTRL_MAX      20   /* rope_ctrl[] slots                          */
#define ROPE_FREE          0xffff
#define ACS_COLLISION_MAX  13   /* ene_c[] / rope_c[] slots                    */
#define CHODO_BANK_MAX     2    /* map banks that can hold chodo cloth         */
#define CHODO_ACS_MAX      15   /* cloths per bank                             */

/* Ke below this is treated as "no damping" -- the ROM compares against a lit4
 * 1.0e-5f, one copy per inline expansion of the same test. */
#define ACS_KE_EPSILON     1.0e-5f

/* Collision tubes built from a model's neck run 400 units along Y: upward for
 * ropes (the rope hangs from above), downward for chodo drapes. */
#define ACS_NECK_TUBE_LEN  400.0f
#define ACS_NECK_TUBE_R    110.0f

/* Rope particle / spring pools.  Both are fixed_array in globals.txt and both
 * are subscripted through the bounds-checked operator[] in the ROM.  Note the
 * spring pool has nine rows against the particle pool's ten -- that is the
 * ROM's sizing, and acsInitRopeSub indexes both with the same work_id, so a
 * twenty-slot rope_ctrl can outrun the spring pool.  Preserved as-is. */
fixed_array<fixed_array<SPRING, 20>, 9>      rope_spring;   /* data 2cca40 */
fixed_array<fixed_array<C_PARTICLE, 20>, 10> rope_particle; /* data 2ccfe0 */

/* Chodo cloth registry, one row per map bank. */
static fixed_array<fixed_array<C_ACS_CTRL, CHODO_ACS_MAX>, CHODO_BANK_MAX> c_acs_ctrl;
                                                            /* bss 421e30 */

/* Models whose neck drives a collision tube.  ene_c is a fixed_array; rope_c is
 * a plain array -- globals.txt types them differently and the ROM matches
 * (acsGetRopeCollisionBuf hands back rope_c with no range check, while
 * acsGetEneCollisionBuf carries the inlined verifyrange). */
static fixed_array<ENE_COLLISION, ACS_COLLISION_MAX> ene_c;  /* bss 422010 */
static ENE_COLLISION rope_c[ACS_COLLISION_MAX];              /* bss 422078 */

static int motClothSetWindTime(WIND_CTRL *wind);
static int motClothWind(CLOTH_CTRL *cloth);

/* ==========================================================================
 *  Player accessory alpha
 *
 *  Two fades: entry 0 starts hidden and fades in, entry 1 starts visible.
 *  The per-entry speeds are function-local initialised arrays in the ROM
 *  (sdata 3ef220 / 3ef228), not a shared table.
 * ======================================================================== */
void InitPlyrAcsAlpha(void)
{
    int i;

    for (i = 0; i < 2; i++)
    {
        if (i == 0)
        {
            plyracs_ctrl[i].stat  = 1;
            plyracs_ctrl[i].alpha = 0;
        }
        else
        {
            plyracs_ctrl[i].stat  = 0;
            plyracs_ctrl[i].alpha = 0x7f;
        }
    }
}

/* Copies the parent's bone matrix into the accessory model's own coordinate
 * array (seven quadwords -- the loop runs to base + 0x1c quadwords), rebuilds
 * the hierarchy from it, then draws at the current fade alpha.
 *
 * The `mdl_p > 0xffffe` guard is g3dDebug.h's pointer sanity check inlined. */
void DispPlyrAcs(u_int *base_p, u_int *mdl_p, ACS_ALPHA *acs_ctrl, u_int bone_id)
{
    SGDCOORDINATE *pCoord;
    float        (*src)[4];
    int            i;

    if (mdl_p <= (u_int *)0xffffe)
    {
        return;
    }

    pCoord = (SGDCOORDINATE *)mdl_p[2];
    src    = (float (*)[4])(bone_id * 0xe0 + base_p[2]);

    for (i = 0; i < 7; i++)
    {
        sceVu0CopyMatrix(pCoord[i].matCoord, src + i * 4);
    }

    sgdCalcBoneCoordinate(pCoord, mdl_p[5] - 1);
    ManmdlSetAlpha(mdl_p, (u_char)acs_ctrl->alpha);
    _gra3dDrawSGD((SGDFILEHEADER *)mdl_p, SRT_REALTIME, (SGDCOORDINATE *)0, -1);
}

/* stat 0 fades in, stat 1 fades out, and the two entries fade at different
 * rates -- the sister's accessory (entry 1) fades symmetrically at 2/frame
 * while the player's snaps out faster than it comes in. */
void PlyrAcsAlphaCtrl(void)
{
    u_char spd[2]  = { 3, 2 };      /* sdata 3ef220 */
    u_char dspd[2] = { 5, 2 };      /* sdata 3ef228 */
    u_int  i;

    for (i = 0; i < 2; i++)
    {
        if (plyracs_ctrl[i].stat == 0)
        {
            plyracs_ctrl[i].alpha += spd[i];

            if (plyracs_ctrl[i].alpha > 0x7f)
            {
                plyracs_ctrl[i].alpha = 0x7f;
            }
        }
        else
        {
            plyracs_ctrl[i].alpha -= dspd[i];

            if (plyracs_ctrl[i].alpha < 0)
            {
                plyracs_ctrl[i].alpha = 0;
            }
        }
    }
}

/* ==========================================================================
 *  Rope slot management
 * ======================================================================== */
void acsInitRopeWork(void)
{
    u_int i;

    for (i = 0; i < ROPE_CTRL_MAX; i++)
    {
        rope_ctrl[i].furn_id = ROPE_FREE;
    }

    for (i = 0; i < ACS_COLLISION_MAX; i++)
    {
        rope_c[i].ani_ctrl = (ANI_CTRL *)0;
    }
}

/* Claims the first free slot.  The ROM's full-table message fires on the last
 * index rather than after it, which is why work_id is bumped to 20 by hand to
 * break the loop -- preserved. */
void acsRopeSetWork(u_int furn_id, u_char acs_no)
{
    u_int work_id;

    work_id = 0;

    while (work_id < ROPE_CTRL_MAX)
    {
        if (rope_ctrl[work_id].furn_id == ROPE_FREE)
        {
            acsInitRopeSub(work_id, furn_id, acs_no);
            return;
        }

        if (work_id == ROPE_CTRL_MAX - 1)
        {
            printf("rope_ctrl buffer is FULL!!!!!!!!!!!\n");
            work_id = ROPE_CTRL_MAX;
        }
        else
        {
            work_id++;
        }
    }
}

void acsRopeReleaseWork(u_int furn_id)
{
    u_int i;

    for (i = 0; i < ROPE_CTRL_MAX; i++)
    {
        if (rope_ctrl[i].furn_id == furn_id)
        {
            rope_ctrl[i].furn_id = ROPE_FREE;
            return;
        }

        if (i == ROPE_CTRL_MAX - 1)
        {
            printf("No rope_ctrl to Release, %d!!!!!!!!!!!!!!!!\n", furn_id);
        }
    }
}

/* -1 for a bad slot index, -2 for a slot that is free. */
int acsRopeGetFurnID(u_int id)
{
    if (id >= ROPE_CTRL_MAX)
    {
        return -1;
    }

    if (rope_ctrl[id].furn_id == ROPE_FREE)
    {
        return -2;
    }

    return rope_ctrl[id].furn_id;
}

float (*acsGetRopePos(u_int furn_id))[4]
{
    u_int i;

    for (i = 0; i < ROPE_CTRL_MAX; i++)
    {
        if (rope_ctrl[i].furn_id == furn_id)
        {
            return (float (*)[4])rope_ctrl[i].top;
        }
    }

    return (float (*)[4])0;
}

/* The three lookups below share one Shift-JIS warning (rodata 39fb60):
 * "Warning : 該当するロープデータがない" -- no matching rope data. */
void acsRopeMoveRequest(u_int furn_id, u_char move_mode, float pow)
{
    u_int i;

    for (i = 0; i < ROPE_CTRL_MAX; i++)
    {
        if (rope_ctrl[i].furn_id == furn_id)
        {
            rope_ctrl[i].pow       = pow;
            rope_ctrl[i].move_mode = move_mode;
            return;
        }
    }

    printf("Warning : 対応するロープデータがない -- no matching rope data\n");
}

void acsRopeMoveStop(u_int furn_id)
{
    u_int i;

    for (i = 0; i < ROPE_CTRL_MAX; i++)
    {
        if (rope_ctrl[i].furn_id == furn_id)
        {
            rope_ctrl[i].move_mode = 0;
            return;
        }
    }

    printf("Warning : 対応するロープデータがない -- no matching rope data\n");
}

u_char acsCheckRopeMoveExec(u_int furn_id)
{
    u_int i;

    for (i = 0; i < ROPE_CTRL_MAX; i++)
    {
        if (rope_ctrl[i].furn_id == furn_id)
        {
            return rope_ctrl[i].move_mode != 0;
        }
    }

    printf("Warning : 対応するロープデータがない -- no matching rope datac\n");

    return 0;
}

/* Random unit vector, used to seed a rope's swing direction.  0x3fffffff is
 * half of MIOPAN_RAND_MAX, so the numerator lands in +/-2^30 and the divisor
 * (MIOPAN_RAND_MAXF, the ROM's 2147483520.0f) brings it to roughly +/-0.5
 * before normalising.  This is the site the range matters most at: on a host
 * whose rand() caps at 0x7fff the subtraction pins all three components at
 * about -0.5, so every rope swings the same way. */
void acsSetMoveDir(float *dir)
{
    dir[0] = (float)(MioPan_Rand() - 0x3fffffff) / MIOPAN_RAND_MAXF;
    dir[1] = (float)(MioPan_Rand() - 0x3fffffff) / MIOPAN_RAND_MAXF;
    dir[2] = (float)(MioPan_Rand() - 0x3fffffff) / MIOPAN_RAND_MAXF;
    dir[3] = 0.0f;

    sceVu0Normalize(dir, dir);
}

/* ==========================================================================
 *  Collision registration
 *
 *  Two identical registries: rope_c for models a rope should avoid, ene_c for
 *  models a chodo drape should avoid.  Each entry pins an ANI_CTRL plus the
 *  model number whose neck position drives the tube.
 * ======================================================================== */
ENE_COLLISION *acsGetRopeCollisionBuf(void)
{
    return rope_c;
}

int acsSetRopeCollision(ANI_CTRL *ani_ctrl, u_short mdl_no)
{
    ENE_COLLISION *buf;
    int            i;

    if (ani_ctrl == (ANI_CTRL *)0)
    {
        printf("ani_ctrl is NULL!!!!!!!!!!  In acsSetRopeCollision\n");
    }

    buf = acsGetRopeCollisionBuf();

    for (i = 0; i < ACS_COLLISION_MAX; i++)
    {
        if (buf[i].ani_ctrl == (ANI_CTRL *)0)
        {
            buf[i].mdl_no   = mdl_no;
            buf[i].ani_ctrl = ani_ctrl;
            return 1;
        }
    }

    printf("Rope Collision is FULL !!!!!!!!!!\n");

    return 0;
}

int acsDelRopeCollision(ANI_CTRL *ani_ctrl)
{
    ENE_COLLISION *buf;
    int            i;

    if (ani_ctrl == (ANI_CTRL *)0)
    {
        printf("ani_ctrl is NULL!!!!!!!!  In acsDelRopeCollision\n");
    }

    buf = acsGetRopeCollisionBuf();

    for (i = 0; i < ACS_COLLISION_MAX; i++)
    {
        if (buf[i].ani_ctrl == ani_ctrl)
        {
            buf[i].mdl_no   = 0;
            buf[i].ani_ctrl = (ANI_CTRL *)0;
            return 1;
        }
    }

    return 0;
}

ENE_COLLISION *acsGetEneCollisionBuf(void)
{
    return &ene_c[0];
}

/* Note the null-ani_ctrl path returns 1, not 0 -- the opposite of the "table
 * full" path.  That is the ROM's shape. */
int acsSetEneCollision(ANI_CTRL *ani_ctrl, u_short mdl_no)
{
    ENE_COLLISION *buf;
    int            i;

    if (ani_ctrl == (ANI_CTRL *)0)
    {
        printf("ani_ctrl is NULL!!!!!!!!!  In acsSetEneCollision");
        return 1;
    }

    buf = acsGetEneCollisionBuf();

    for (i = 0; i < ACS_COLLISION_MAX; i++)
    {
        if (buf[i].ani_ctrl == (ANI_CTRL *)0)
        {
            buf[i].mdl_no   = mdl_no;
            buf[i].ani_ctrl = ani_ctrl;
            return 1;
        }
    }

    printf("Chodo Collision is FULL!!!!!!!!\n");

    return 0;
}

int acsDelEneCollision(ANI_CTRL *ani_ctrl)
{
    ENE_COLLISION *buf;
    int            i;

    if (ani_ctrl == (ANI_CTRL *)0)
    {
        printf("ani_ctrl is NULL!!!!!!!!!  In acsDelEneCollision\n");
        return 1;
    }

    buf = acsGetEneCollisionBuf();

    for (i = 0; i < ACS_COLLISION_MAX; i++)
    {
        if (buf[i].ani_ctrl == ani_ctrl)
        {
            buf[i].mdl_no   = 0;
            buf[i].ani_ctrl = (ANI_CTRL *)0;
            return 1;
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  acsRopeMakeCollision / acsChodoMakeCollision
 *
 *  Build a COLLISION_DAT list plus its TUBEs in the rope's / cloth's own local
 *  space.  The incoming matrix is normalised column-by-column and rescaled, so
 *  the inverse is a pure rotation+scale with no shear.  Each registered model
 *  contributes one vertical tube through its neck.
 *
 *  The two differ only in which registry they walk and which way the tube
 *  extends: a rope hangs from above so its tube runs up, a drape hangs down so
 *  its tube runs down.  Both terminate the list with a zeroed record and both
 *  clamp at 12 entries -- the 13th slot is the terminator.
 * ------------------------------------------------------------------------ */
int acsRopeMakeCollision(COLLISION_DAT *collision, float (*mtx)[4], TUBE *tube,
                         float scale)
{
    ENE_COLLISION *buf;
    COLLISION_DAT *dst;
    float          pos[4];
    float          inv[4][4];
    float          c2w[4][4];
    int            num;
    int            i;

    num = 0;

    sceVu0CopyMatrix(c2w, mtx);
    sceVu0Normalize(c2w[0], c2w[0]);
    sceVu0Normalize(c2w[1], c2w[1]);
    sceVu0Normalize(c2w[2], c2w[2]);
    sceVu0ScaleVector(c2w[0], c2w[0], scale);
    sceVu0ScaleVector(c2w[1], c2w[1], scale);
    sceVu0ScaleVector(c2w[2], c2w[2], scale);
    motSetInvMatrix(inv, c2w);

    buf = acsGetRopeCollisionBuf();
    dst = collision;

    for (i = 0; i < ACS_COLLISION_MAX; i++)
    {
        if (buf[i].ani_ctrl == (ANI_CTRL *)0)
        {
            continue;
        }

        dst->type    = 1;
        dst->bone_id = 0;
        dst->dat     = tube;
        dst++;
        num++;

        GetMdlNeckPos(pos, buf[i].ani_ctrl, buf[i].mdl_no);
        sceVu0ApplyMatrix(pos, inv, pos);
        sceVu0CopyVector(tube->p0, pos);
        sceVu0CopyVector(tube->p1, pos);
        tube->r      = ACS_NECK_TUBE_R;
        tube->axis   = 0;
        tube->p1[1] += ACS_NECK_TUBE_LEN;
        tube++;
    }

    if (num > 12)
    {
        printf("!!!!!rope collision over!!!!!");
        num = 12;
    }

    collision[num].dat     = (void *)0;
    collision[num].type    = 0;
    collision[num].bone_id = 0;

    return 1;
}

int acsChodoMakeCollision(COLLISION_DAT *collision, SGDCOORDINATE *cp, TUBE *tube,
                          float scale)
{
    ENE_COLLISION *buf;
    COLLISION_DAT *dst;
    float          pos[4];
    float          inv[4][4];
    float          c2w[4][4];
    int            num;
    int            i;

    num = 0;

    sceVu0CopyMatrix(c2w, cp->matLocalWorld);
    sceVu0Normalize(c2w[0], c2w[0]);
    sceVu0Normalize(c2w[1], c2w[1]);
    sceVu0Normalize(c2w[2], c2w[2]);
    sceVu0ScaleVector(c2w[0], c2w[0], scale);
    sceVu0ScaleVector(c2w[1], c2w[1], scale);
    sceVu0ScaleVector(c2w[2], c2w[2], scale);
    motSetInvMatrix(inv, c2w);

    buf = acsGetEneCollisionBuf();
    dst = collision;

    for (i = 0; i < ACS_COLLISION_MAX; i++)
    {
        if (buf[i].ani_ctrl == (ANI_CTRL *)0)
        {
            continue;
        }

        dst->type    = 1;
        dst->bone_id = 0;
        dst->dat     = tube;
        dst++;
        num++;

        GetMdlNeckPos(pos, buf[i].ani_ctrl, buf[i].mdl_no);
        sceVu0ApplyMatrix(pos, inv, pos);
        sceVu0CopyVector(tube->p0, pos);
        sceVu0CopyVector(tube->p1, pos);
        tube->r      = ACS_NECK_TUBE_R;
        tube->axis   = 0;
        tube->p1[1] -= ACS_NECK_TUBE_LEN;
        tube++;
    }

    if (num > 12)
    {
        printf("!!!!!chodo collision over!!!!!");
        num = 12;
    }

    collision[num].dat     = (void *)0;
    collision[num].type    = 0;
    collision[num].bone_id = 0;

    return 1;
}

/* ==========================================================================
 *  Collision primitives
 *
 *  All three take the particle's current position by pointer and push it out
 *  of the volume in place, optionally bleeding off the relative velocity by
 *  Ke.  Answering non-zero means "this particle was moved".
 * ======================================================================== */

/* Push out along the centre-to-particle direction by the penetration depth. */
u_char acsCheckCollisionSphere(SPHERE *sphere, float *current, float *relative_v,
                               float Ke)
{
    float tmp[4];
    float v[4];
    float v1[4];
    float len;
    float r;

    r = sphere->r;

    sceVu0SubVector(tmp, current, sphere->center);
    len = g3dxVu0Sqrt(sceVu0InnerProduct(tmp, tmp));

    if (len >= r)
    {
        return 0;
    }

    sceVu0Normalize(v, tmp);
    sceVu0ScaleVectorXYZ(v, v, r - len);

    if (Ke > ACS_KE_EPSILON)
    {
        sceVu0CopyVector(v1, relative_v);
        sceVu0ScaleVectorXYZ(v1, v1, -Ke);
        sceVu0AddVector(v, v, v1);
    }

    sceVu0AddVector(current, current, v);

    return 1;
}

/* plane->p0 is a unit normal and plane->r the plane constant, so the four
 * components form a plane equation and the test is a 4-component dot product
 * (the vmul / vaddabc / vmaddabc / vmaddbc idiom in the ROM).  `ofs` biases the
 * surface by ofs/10 units, which is how per-particle thickness is applied. */
u_char acsCheckCollisionPlane(CPLANE *plane, float *current, float *relative_v,
                              float Ke, int ofs)
{
    float p_vec[4];
    float tmp[4];
    float d;

    sceVu0CopyVector(p_vec, plane->p0);
    p_vec[3] = plane->r;

    d = p_vec[0] * current[0] + p_vec[1] * current[1] +
        p_vec[2] * current[2] + p_vec[3] * current[3];
    d -= (float)ofs / 10.0f;

    if (d >= 0.0f)
    {
        return 0;
    }

    sceVu0ScaleVector(tmp, plane->p0, -d);
    sceVu0AddVector(current, current, tmp);

    if (Ke > ACS_KE_EPSILON)
    {
        sceVu0ScaleVector(tmp, relative_v, -Ke);
        sceVu0AddVector(current, current, tmp);
    }

    return 1;
}

/* Capsule test, split into the two end caps and the cylindrical middle.
 *
 * The middle case has an extra guard: if the particle is well inside the tube
 * (within 0.8 r) *and* moving faster than one unit per step, it is teleported
 * back to its previous position rather than pushed out -- that is the ROM's
 * tunnelling escape hatch, and it only fires when the caller supplied old_c. */
u_char acsCheckCollisionTube(TUBE *tube, float *current, float *relative_v,
                             float *old_c, float Ke)
{
    float p0[4], p1[4];
    float v0[4], v2[4];
    float dir0[4], dir1[4];
    float AP[4], PA[4], P1P0[4], normal[4];
    float unit[4];
    float n[4];
    float push[4];
    float d;
    float len;
    float r;

    sceVu0CopyVector(p0, tube->p0);
    sceVu0CopyVector(p1, tube->p1);
    sceVu0SubVector(v0, p0, p1);

    /* Beyond the p0 cap? */
    sceVu0SubVector(dir0, current, p0);

    if (sceVu0InnerProduct(dir0, v0) > 0.0f)
    {
        len = g3dxVu0Sqrt(sceVu0InnerProduct(dir0, dir0));

        if (len >= tube->r)
        {
            return 0;
        }

        sceVu0Normalize(push, dir0);
        sceVu0ScaleVector(push, push, tube->r - len);
        sceVu0AddVector(current, current, push);

        return 1;
    }

    /* Beyond the p1 cap? */
    sceVu0SubVector(dir1, current, p1);
    sceVu0ScaleVector(v0, v0, -1.0f);

    if (sceVu0InnerProduct(dir1, v0) > 0.0f)
    {
        len = g3dxVu0Sqrt(sceVu0InnerProduct(dir1, dir1));

        if (len >= tube->r)
        {
            return 0;
        }

        sceVu0Normalize(push, dir1);
        sceVu0ScaleVector(push, push, tube->r - len);
        sceVu0AddVector(current, current, push);

        return 1;
    }

    /* Between the caps: drop a perpendicular onto the axis. */
    sceVu0SubVector(PA, current, p0);
    sceVu0SubVector(P1P0, p1, p0);
    sceVu0Normalize(unit, P1P0);

    d   = sceVu0InnerProduct(unit, PA);
    len = g3dxVu0Sqrt(sceVu0InnerProduct(P1P0, P1P0));

    sceVu0ScaleVector(v2, P1P0, d / len);
    sceVu0ScaleVector(AP, PA, -1.0f);
    sceVu0AddVector(normal, v2, AP);

    len = g3dxVu0Sqrt(sceVu0InnerProduct(normal, normal));
    r   = tube->r;

    if (len < r * 0.8f)
    {
        if (g3dxVu0Sqrt(sceVu0InnerProduct(relative_v, relative_v)) > 1.0f)
        {
            if (old_c != (float *)0)
            {
                sceVu0CopyVector(current, old_c);
                return 1;
            }
        }

        r = tube->r;
    }

    if (len >= r)
    {
        return 0;
    }

    sceVu0Normalize(n, normal);
    sceVu0ScaleVector(n, n, -(r - len));
    sceVu0AddVector(current, current, n);

    sceVu0ScaleVector(push, relative_v, -Ke);
    sceVu0AddVector(current, current, push);

    return 1;
}

/* ==========================================================================
 *  Chodo (map drape) registry and wind
 * ======================================================================== */
int acsChodoInitCloth(void)
{
    int i;

    for (i = 0; i < ACS_COLLISION_MAX; i++)
    {
        ene_c[i].ani_ctrl = (ANI_CTRL *)0;
    }

    acsChodoDel(0);
    acsChodoDel(1);

    return 1;
}

/* The out-of-range message names acsChodoSetCloth, not this function.  ROM
 * text, kept. */
int acsChodoDel(int id)
{
    int i;

    if ((u_int)id > 2)
    {
        printf("error! map bank id = %d (in acsChodoSetCloth)\n", id);
    }

    for (i = 0; i < CHODO_ACS_MAX; i++)
    {
        c_acs_ctrl[id][i].c_cloth_ctrl = (CLOTH_CTRL *)0;
        c_acs_ctrl[id][i].addr         = (u_int *)0;
        c_acs_ctrl[id][i].mdl_no       = 0;
        c_acs_ctrl[id][i].key          = -1;
    }

    return 1;
}

/* Wind direction is a Y rotation applied to +Z, so `rot` is a compass bearing
 * in radians clamped to [-pi, pi].  Every cloth registered under `key` picks
 * it up.  The scan stops at the first empty slot in each bank, so the registry
 * is expected to stay dense. */
int acsChodoSetWind(int key, float rot, float pow, int cycle)
{
    CLOTH_CTRL *cloth;
    float       y_rot_m[4][4];
    float       z_ziku[4];
    float       r;
    int         found;
    int         bank;
    int         i;

    found = 0;

    memset(z_ziku, 0, 0x10);
    z_ziku[2] = 1.0f;

    for (bank = 0; bank < CHODO_BANK_MAX; bank++)
    {
        for (i = 0; i < CHODO_ACS_MAX; i++)
        {
            if (c_acs_ctrl[bank][i].c_cloth_ctrl == (CLOTH_CTRL *)0)
            {
                break;
            }

            if (c_acs_ctrl[bank][i].key != key)
            {
                continue;
            }

            cloth = c_acs_ctrl[bank][i].c_cloth_ctrl;

            cloth->w_ctrl.pow   = pow;
            cloth->w_ctrl.cycle = cycle;

            r = rot;
            if (r < -3.14159274f)
            {
                r = -3.14159274f;
            }
            else if (r > 3.14159274f)
            {
                r = 3.14159274f;
            }

            found = 1;

            sceVu0UnitMatrix(y_rot_m);
            sceVu0RotMatrixY(y_rot_m, y_rot_m, r);
            sceVu0ApplyMatrix(cloth->w_ctrl.dir, y_rot_m, z_ziku);
            motClothSetWindTime(&cloth->w_ctrl);
            cloth->w_ctrl.sta = 1;
        }
    }

    if (found == 0)
    {
        printf("WARNING can not set wind (In acsChodoSetWind)\n");
    }

    return found;
}

int acsChodoResetWind(int key)
{
    CLOTH_CTRL *cloth;
    int         found;
    int         bank;
    int         i;

    found = 0;

    for (bank = 0; bank < CHODO_BANK_MAX; bank++)
    {
        for (i = 0; i < CHODO_ACS_MAX; i++)
        {
            if (c_acs_ctrl[bank][i].c_cloth_ctrl == (CLOTH_CTRL *)0)
            {
                break;
            }

            if (c_acs_ctrl[bank][i].key != key)
            {
                continue;
            }

            found = 1;
            cloth = c_acs_ctrl[bank][i].c_cloth_ctrl;

            cloth->w_ctrl.sta    = 0;
            cloth->w_ctrl.pow    = 0.0f;
            cloth->w_ctrl.cycle  = 0;
            cloth->w_ctrl.dir[0] = 0.0f;
            cloth->w_ctrl.dir[1] = 0.0f;
            cloth->w_ctrl.dir[2] = 0.0f;
            cloth->w_ctrl.dir[3] = 0.0f;
        }
    }

    if (found == 0)
    {
        printf("WARNING!  can not reset wind (In acsChodoResetWind)\n");
    }

    return found;
}

/* --------------------------------------------------------------------------
 *  motClothWind
 *
 *  Adds this frame's wind to a cloth's particle velocities.  The cycle has
 *  three phases: a full-strength gust up to w_ctrl.strong, a dead band, and a
 *  0.3x tail past w_ctrl.weak.  Crossing frame 0 of the cycle re-rolls both
 *  boundaries, so no two gusts are the same length.
 *
 *  Only about half the particles are pushed on any given frame -- the coin
 *  flip per particle is what makes the sheet ripple instead of translating.
 *
 *  Answers 1 whenever wind is enabled at all, regardless of phase.
 * ------------------------------------------------------------------------ */
static int motClothWind(CLOTH_CTRL *cloth)
{                                                                       /* 1778 */
    float wind[4];
    int   t;
    int   blow;
    int   i;

    if (cloth->w_ctrl.sta == 0)                                         /* 1787 */
    {
        return 0;
    }

    sceVu0ScaleVector(wind, cloth->w_ctrl.dir, cloth->w_ctrl.pow);      /* 1790 */

    t = (int)sys_wrk.count % cloth->w_ctrl.cycle;                       /* 1791 */

    if (t == 0)                                                         /* 1793 */
    {
        motClothSetWindTime(&cloth->w_ctrl);                            /* 1795 */
    }

    blow = 0;

    if (t < cloth->w_ctrl.strong)                                       /* 1799 */
    {
        blow = 1;                                                       /* 1800 */
    }
    else if (t > cloth->w_ctrl.weak)                                    /* 1802 */
    {
        blow = 1;
        sceVu0ScaleVector(wind, wind, 0.3f);                            /* 1804 */
    }

    if (blow != 0)                                                      /* 1807 */
    {
        for (i = 0; i < (int)(u_short)cloth->p_num; i++)                /* 1808 */
        {
            if (motGetRandom(1.0f, -1.0f) > 0.0f)                       /* 1809 */
            {
                sceVu0AddVector(cloth->particle[i].v, cloth->particle[i].v, wind);
                                                                        /* 1810 */
            }
        }
    }

    return 1;                                                           /* 1816 */
}                                                                       /* 1818 */

/* Splits the wind cycle into a gust and a lull, each jittered by +/-15 frames
 * around a third of the cycle.  Both draws use the same (15, -15) range. */
static int motClothSetWindTime(WIND_CTRL *wind)
{
    int third;

    third = wind->cycle / 3;

    wind->strong = third + (int)motGetRandom(15.0f, -15.0f);
    wind->weak   = wind->cycle - (third + (int)motGetRandom(15.0f, -15.0f));

    return 1;
}

/* ==========================================================================
 *  Cloth reset
 *
 *  Walks the CLOTH_CTRL chain (terminated by a cdat with a null dat) and
 *  re-seeds every particle's rest position from the model's own vertices, then
 *  flags the cloth for a hard reset on the next step.
 * ======================================================================== */
int acsResetCloth(ANI_CTRL *ani_ctrl)
{
    CLOTH_CTRL *cloth;
    CLOTH_DAT  *cdat;
    SGDFILEHEADER       *sgd_p;
    float      *vtx;
    int         i;

    if (ani_ctrl == (ANI_CTRL *)0)
    {
        return 0;
    }

    cloth = ani_ctrl->cloth_ctrl;

    if (cloth == (CLOTH_CTRL *)0)
    {
        return 0;
    }

    for (cdat = cloth->cdat; cdat->dat != (CLOTH *)0; cdat = cloth->cdat)
    {
        cloth->reset_flg |= 5;

        sgd_p = (SGDFILEHEADER*)GetFileInPak(ani_ctrl->mpk_p, cdat->sgd_id);
        vtx   = (float*)sgd_p->pVectorInfo->aAddress[SVA_UNIQUE].pvVertex.get();

        for (i = 0; i < (int)(u_short)cloth->p_num; i++)
        {
            sceVu0CopyVector(vtx + i * 4, cloth->particle[i].org);
        }

        cloth++;
    }

    return 1;
}

void SetLWS2(SGDCOORDINATE *cp)
{
    sceVu0CopyMatrix(cp->matLocalWorld, cp->matCoord);
    cp->bCalc = 1;
}

/* ==========================================================================
 *  Rope simulation
 *
 *  A rope is a chain of C_PARTICLE joined by SPRING, integrated with Verlet:
 *  velocity is not stored between frames but recovered as (p - old) * Kd at the
 *  end of each step.  One step is
 *
 *      v += gravity;  p += v                     (integrate)
 *      per spring: pull both ends halfway to ldef (relax)
 *      pin the first rdat->fixed_num particles   (constrain)
 *      v = (p - old) * Kd;  old = p              (damp + store)
 *
 *  acsInitRopeSub runs that same step thirty times with no collision to settle
 *  a freshly-built rope into its hanging pose before it is ever drawn.
 *
 *  DEVIATION: the ROM inlines the three phases below at both call sites --
 *  there are no such symbols in functions.txt.  They are factored out here
 *  only to avoid carrying the same forty lines twice; the code is otherwise
 *  literal.  Un-factor them if a strict symbol match is ever wanted.
 * ======================================================================== */

/* Relax every spring: each end moves half the length error along the spring. */
static void acsRopeRelaxSprings(ROPE_CTRL *rope, C_PARTICLE *particle)
{
    C_PARTICLE *p1;
    C_PARTICLE *p2;
    float       d[4];
    float       corr[4];
    float       len;
    u_int       i;

    for (i = 0; i < rope->spring_num; i++)
    {
        p1 = particle + rope->spring[i].p1;
        p2 = particle + rope->spring[i].p2;

        sceVu0SubVector(d, p1->p, p2->p);
        len = g3dxVu0Sqrt(sceVu0InnerProduct(d, d));

        sceVu0ScaleVector(corr, d, ((rope->spring[i].ldef - len) * 0.5f) / len);
        sceVu0AddVector(p1->p, p1->p, corr);
        sceVu0SubVector(p2->p, p2->p, corr);
    }
}

/* The fixed head of the rope is pinned straight down the local Y axis at the
 * offsets rdat->vtx[] gives -- the same table the particles were seeded from. */
static void acsRopePinFixed(ROPE_DAT *rdat, C_PARTICLE *particle)
{
    u_int i;

    for (i = 0; i < rdat->fixed_num; i++)
    {
        particle[i].p[0] = 0.0f;
        particle[i].p[1] = rdat->vtx[i];
        particle[i].p[2] = 0.0f;
        particle[i].p[3] = 1.0f;
    }
}

/* Verlet close-out: recover the velocity from the frame's actual displacement
 * (so the spring and collision corrections above feed back into it), scale by
 * the drag coefficient, and snapshot the position for next frame. */
static void acsRopeDampAndStore(ROPE_CTRL *rope, ROPE_DAT *rdat, C_PARTICLE *particle)
{
    u_int i;

    for (i = 0; i < rope->p_num; i++)
    {
        sceVu0SubVector(particle[i].v, particle[i].p, particle[i].old);
        sceVu0ScaleVector(particle[i].v, particle[i].v, rdat->Kd);
    }

    for (i = 0; i < rope->p_num; i++)
    {
        sceVu0CopyVector(particle[i].old, particle[i].p);
    }
}

/* Build a rope into slot work_id.  rdat->vtx[] is a list of Y offsets running
 * down the rope, terminated by a negative entry; the ROM scales it by 25 to get
 * model units.  Springs join consecutive particles and take their rest length
 * from the seeded positions. */
void acsInitRopeSub(u_int work_id, u_int furn_id, u_int type)
{                                                                       /* 209 */
    ROPE_CTRL  *rope;
    ROPE_DAT   *rdat;
    C_PARTICLE *particle;
    float       d[4];
    float       gravity[4];
    u_char      p_num;
    u_char      spring_num;
    u_int       i;
    int         n;

    rope = &rope_ctrl[work_id];

    rope->particle = rope_particle[work_id].data();                     /* 215 */
    rope->rdat     = rope_tbl[type];
    rope->spring   = rope_spring[work_id].data();

    rope->furn_id   = furn_id;                                          /* 219 */
    rope->stat      = 1;
    rope->pow       = 1.0f;
    rope->move_mode = 0;
    rope->top[0]    = 0.0f;
    rope->top[1]    = 0.0f;
    rope->top[2]    = 0.0f;
    rope->top[3]    = 0.0f;

    acsSetMoveDir(rope->dir);                                           /* 228 */

    /* Seed the chain.  vtx[] is walked until it goes negative, so the table
     * itself decides how many particles a rope type has. */
    rdat     = rope->rdat;
    particle = rope->particle;
    p_num    = 0;
    spring_num = 0;

    /* PORT GUARD, not ROM behaviour.  rope_tbl[] now carries the ROM's real
     * data, but slots 14 and 15 are NULL there too, so a request for either
     * type would fault here exactly as it would have on the PS2.  Nothing is
     * known to ask for them; this turns the fault into an inert rope instead
     * of taking the process down while the rest of the port is still moving. */
    if (rdat == (ROPE_DAT *)0)
    {
        rope->p_num      = 0;
        rope->spring_num = 0;
        rope->stat       = 0;
        return;
    }

    if (rdat->vtx[0] >= 0.0f)                                           /* 231 */
    {
        i = 0;

        do
        {
            spring_num = p_num;

            particle[i].p[0] = 0.0f;
            particle[i].p[1] = rdat->vtx[i];
            particle[i].p[2] = 0.0f;
            particle[i].p[3] = 0.0f;

            sceVu0CopyVector(particle[i].old, particle[i].p);
            sceVu0ScaleVector(particle[i].p,   particle[i].p,   25.0f);
            sceVu0ScaleVector(particle[i].old, particle[i].old, 25.0f);

            particle[i].v[0] = 0.0f;
            particle[i].v[1] = 0.0f;
            particle[i].v[2] = 0.0f;
            particle[i].v[3] = 0.0f;

            particle[i].old[3] = 1.0f;
            particle[i].p[3]   = 1.0f;

            p_num++;
            i++;

            /* The ROM tests the freshly-zeroed particle[i-1].v[0] against the
             * next table entry; that register simply holds 0.0f, so this is
             * the same comparison spelled directly. */
        } while (0.0f <= rdat->vtx[i]);
    }

    rope->p_num      = p_num;                                           /* 244 */
    rope->spring_num = spring_num;

    for (i = 0; i < rope->spring_num; i++)                              /* 250 */
    {
        rope->spring[i].p1 = (u_short)i;
        rope->spring[i].p2 = (u_short)(i + 1);

        sceVu0SubVector(d, particle[rope->spring[i].p1].p,
                           particle[rope->spring[i].p2].p);
        rope->spring[i].ldef = g3dxVu0Sqrt(sceVu0InnerProduct(d, d));
    }

    /* Thirty settle iterations, no collision -- the rope is not in the world
     * yet, it is only finding its own hanging shape. */
    for (n = 0; n < 30; n++)                                            /* 264 */
    {
        gravity[0] = 0.0f;
        gravity[1] = rope->rdat->gravity;
        gravity[2] = 0.0f;
        gravity[3] = 0.0f;

        for (i = 0; i < rope->p_num; i++)                               /* 271 */
        {
            sceVu0AddVector(particle[i].v, particle[i].v, gravity);
            sceVu0AddVector(particle[i].p, particle[i].p, particle[i].v);
        }

        acsRopeRelaxSprings(rope, particle);                            /* 276 */
        acsRopePinFixed(rope->rdat, particle);                          /* 282 */
        acsRopeDampAndStore(rope, rope->rdat, particle);                /* 287 */
    }
}                                                                       /* 290 */

/* --------------------------------------------------------------------------
 *  acsCalcCoordinate
 *
 *  Turns the particle chain into the model's bone matrices.  Segment i gets a
 *  frame whose X axis is the segment direction: the rotation is built as the
 *  axis-angle taking +X onto that direction, applied to +Y and +Z to fill in
 *  the other two rows, then composed with the root coordinate and transposed.
 *
 *  cp[0] is the rope's own root; cp[1..spring_num] are the segments.
 * ------------------------------------------------------------------------ */
void acsCalcCoordinate(SGDCOORDINATE *cp, ROPE_CTRL *rope)
{                                                                       /* 446 */
    float trans[10][4];
    float vec[4];
    float x[4];
    float y[4];
    float z[4];
    float rot[4][4];
    float axis[4];
    float angle;
    u_int i;

    memset(x, 0, 0x10);                                                 /* 449 */
    x[0] = 1.0f;
    memset(y, 0, 0x10);                                                 /* 451 */
    y[1] = 1.0f;
    memset(z, 0, 0x10);                                                 /* 453 */
    z[2] = 1.0f;

    for (i = 0; i < rope->spring_num; i++)                              /* 455 */
    {
        sceVu0UnitMatrix(cp[i + 1].matCoord);
    }

    for (i = 0; i < rope->p_num; i++)                                   /* 456 */
    {
        sceVu0CopyVector(trans[i], rope->particle[i].p);                /* 457 */
    }

    for (i = 0; i < rope->spring_num; i++)                              /* 461 */
    {
        sceVu0SubVector(vec, trans[i + 1], trans[i]);                   /* 462 */
        sceVu0Normalize(cp[i + 1].matCoord[0], vec);                    /* 463 */
    }

    for (i = 0; i < rope->spring_num; i++)                              /* 467 */
    {
        sceVu0UnitMatrix(rot);                                          /* 471 */

        sceVu0OuterProduct(axis, x, cp[i + 1].matCoord[0]);             /* 472 */
        sceVu0Normalize(axis, axis);                                    /* 473 */
        angle = g3dAcosf(sceVu0InnerProduct(x, cp[i + 1].matCoord[0])); /* 474 */

        /* g3dxVu0.h's quaternion form, inlined here in the ROM. */
        g3dxVu0RotMatrixAxis(rot, axis, angle);                         /* 475 */

        sceVu0ApplyMatrix(cp[i + 1].matCoord[1], rot, y);               /* 477 */
        sceVu0ApplyMatrix(cp[i + 1].matCoord[2], rot, z);               /* 478 */

        /* Compose with the root.  Operand order is the ROM's -- per
         * sceVu0MulMatrix(m0, m1, m2) this yields m0 = m2 * m1. */
        sceVu0CopyMatrix(rot, cp->matCoord);                            /* 482 */
        sceVu0MulMatrix(cp[i + 1].matCoord, rot, cp[i + 1].matCoord);   /* 484 */
        sceVu0TransMatrix(cp[i + 1].matCoord, cp[i + 1].matCoord, trans[i]);
                                                                        /* 485 */
    }

    for (i = 0; i <= rope->spring_num; i++)                             /* 497 */
    {
        SetLWS2(&cp[i]);                                                /* 498 */
    }
}                                                                       /* 499 */

/* --------------------------------------------------------------------------
 *  Rope motion modes
 * ------------------------------------------------------------------------ */
void acsRopeMoveCtrl(ROPE_CTRL *rope)
{                                                                       /* 525 */
    switch (rope->move_mode)
    {
    case 1:
        acsRopeMoveWind(rope, 0);                                       /* 529 */
        break;

    case 2:
        acsRopeMoveWind(rope, 1);                                       /* 532 */
        break;

    case 3:
        acsRopeMoveVib(rope);                                           /* 535 */
        break;

    case 0:
    case 4:
    default:
        break;
    }
}                                                                       /* 539 */

/* Steady wind on every particle, gusting 40 frames in every 120.  With
 * dir_cng set the direction is also re-rolled roughly every 90 frames, and
 * only half the time (motGetRandom decides), so gusts wander. */
void acsRopeMoveWind(ROPE_CTRL *rope, char dir_cng)
{                                                                       /* 552 */
    float wind[4];
    u_int i;

    if ((dir_cng != 0) && (sys_wrk.count % 90 == 0))                    /* 553 */
    {
        if (motGetRandom(1.0f, -1.0f) > 0.0f)                           /* 554 */
        {
            acsSetMoveDir(rope->dir);                                   /* 555 */
        }
    }

    sceVu0ScaleVector(wind, rope->dir, rope->pow);                      /* 571 */

    if ((sys_wrk.count % 120) < 40)                                     /* 575 */
    {
        for (i = 0; i < rope->p_num; i++)                               /* 578 */
        {
            sceVu0AddVector(rope->particle[i].v, rope->particle[i].v, wind);
                                                                        /* 579 */
        }
    }
}                                                                       /* 581 */

/* A single shake: fresh direction every call, and only the last two particles
 * are kicked, so the rope whips at the free end. */
void acsRopeMoveVib(ROPE_CTRL *rope)
{                                                                       /* 588 */
    float wind[4];
    u_int i;

    acsSetMoveDir(rope->dir);                                           /* 593 */
    sceVu0ScaleVector(wind, rope->dir, rope->pow);                      /* 594 */

    for (i = rope->p_num - 2; i < rope->p_num; i++)                     /* 595 */
    {
        sceVu0AddVector(rope->particle[i].v, rope->particle[i].v, wind);
                                                                        /* 596 */
    }
}                                                                       /* 597 */

/* --------------------------------------------------------------------------
 *  acsMoveRope
 *
 *  One frame of rope simulation for the rope bound to furn_id.
 *
 *  The integration runs in the furniture's *local* space: the world matrix is
 *  normalised, inverted, and gravity plus every particle is rotated into local
 *  space first (m1 = transpose of the rotation part).  Collision is the
 *  exception -- each particle is pushed out to world space, tested, and the
 *  result rotated back, because the collision tubes are built in world space.
 * ------------------------------------------------------------------------ */
void acsMoveRope(u_int furn_id, SGDCOORDINATE *furn_cp)
{                                                                       /* 605 */
    ROPE_CTRL     *rope;
    ROPE_DAT      *rdat;
    C_PARTICLE    *current;
    COLLISION_DAT  collision[ACS_COLLISION_MAX];
    TUBE           tube[ACS_COLLISION_MAX];
    COLLISION_DAT *c;
    float          l2w[4][4];
    float          w2l[4][4];
    float          m[4][4];
    float          m1[4][4];
    float          w_mtx[4][4];
    float          gravity[4];
    float          p[4];
    float          v[4];
    u_char         hit;
    u_int          n;
    u_int          i;

    rope = (ROPE_CTRL *)0;

    for (i = 0; i < ROPE_CTRL_MAX; i++)                                 /* 611 */
    {
        if (rope_ctrl[i].furn_id == furn_id)
        {
            rope = &rope_ctrl[i];
            break;
        }

        if (i == ROPE_CTRL_MAX - 1)
        {
            printf("No rope_ctrl to control, %d\n", furn_id);
            return;
        }
    }

    if (rope == (ROPE_CTRL *)0)
    {
        return;
    }

    rdat    = rope->rdat;
    current = rope->particle;

    if (rope->stat == 0)                                                /* 623 */
    {
        return;
    }

    sceVu0CopyMatrix(l2w, furn_cp->matCoord);                           /* 624 */
    sceVu0Normalize(l2w[0], l2w[0]);
    sceVu0Normalize(l2w[1], l2w[1]);
    sceVu0Normalize(l2w[2], l2w[2]);
    sceVu0InversMatrix(w2l, l2w);                                       /* 629 */

    /* m keeps the rotation with a zeroed translation row; m1 is its transpose,
     * i.e. the inverse rotation, which is what takes world into local. */
    sceVu0CopyMatrix(m, l2w);                                           /* 632 */
    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 0.0f;
    sceVu0TransposeMatrix(m1, m);                                       /* 634 */

    gravity[0] = 0.0f;                                                  /* 635 */
    gravity[1] = rdat->gravity;
    gravity[2] = 0.0f;
    gravity[3] = 0.0f;
    sceVu0ApplyMatrix(gravity, m1, gravity);                            /* 641 */

    for (i = 0; i < rope->p_num; i++)                                   /* 643 */
    {
        sceVu0ApplyMatrix(current[i].p, m1, current[i].p);              /* 647 */
        sceVu0ApplyMatrix(current[i].v, m1, current[i].v);              /* 648 */
    }

    acsRopeMoveCtrl(rope);                                              /* 652 */

    for (i = 0; i < rope->p_num; i++)                                   /* 653 */
    {
        sceVu0AddVector(current[i].v, current[i].v, gravity);           /* 654 */
        sceVu0AddVector(current[i].p, current[i].p, current[i].v);      /* 655 */
    }

    acsRopeRelaxSprings(rope, current);                                 /* 663 */

    sceVu0UnitMatrix(w_mtx);                                            /* 697 */
    acsRopeMakeCollision(collision, w_mtx, tube, 1.0f);                 /* 699 */

    for (i = 0; i < rope->p_num; i++)                                   /* 703 */
    {
        current[i].v[3] = 0.0f;                                         /* 707 */
        current[i].p[3] = 1.0f;

        sceVu0ApplyMatrix(p, l2w, current[i].p);                        /* 710 */
        sceVu0ApplyMatrix(v, l2w, current[i].v);                        /* 711 */

        n = 0;

        for (c = collision; c->dat != (void *)0; c++)                   /* 713 */
        {
            if (n > 4)                                                  /* 714 */
            {
                printf("Warning : Collision data is too large\n");
                break;
            }
            n++;

            hit = 0;

            if (c->type == 0)                                           /* 720 */
            {
                hit = acsCheckCollisionSphere((SPHERE *)c->dat, p, v, rdat->Ke);
            }
            else if (c->type == 1)                                      /* 723 */
            {
                hit = acsCheckCollisionTube((TUBE *)c->dat, p, v, (float *)0,
                                            rdat->Ke);                  /* 724 */
            }

            if (hit != 0)                                               /* 726 */
            {
                sceVu0ApplyMatrix(current[i].p, w2l, p);                /* 732 */
            }
        }
    }

    for (i = 0; i < rope->p_num; i++)                                   /* 777 */
    {
        sceVu0ApplyMatrix(current[i].p, m, current[i].p);               /* 778 */
        sceVu0ApplyMatrix(current[i].v, m, current[i].v);               /* 779 */
    }

    acsRopePinFixed(rdat, current);                                     /* 783 */
    acsRopeDampAndStore(rope, rdat, current);                           /* 793 */

    acsCalcCoordinate(furn_cp, rope);                                   /* 802 */
}                                                                       /* 803 */

/* Drives a rope whose root matrix is supplied directly rather than read from a
 * furniture coordinate -- the SGD's own coordinate array is reset to identity
 * first, then block 0 is overwritten with `mat`. */
void acsMoveRopeEx(u_int furn_id, HeaderSection *sgd_p, float (*mat)[4])
{                                                                       /* 808 */
    SGDCOORDINATE *furn_cp;
    u_int          i;

    furn_cp = sgd_p->coordp;

    for (i = 1; i < sgd_p->blocks - 1; i++)                             /* 811 */
    {
        sceVu0UnitMatrix(furn_cp[i].matCoord);                          /* 812 */
    }

    sceVu0CopyMatrix(furn_cp->matCoord, mat);                           /* 813 */

    acsMoveRope(furn_id, furn_cp);                                      /* 817 */
}

/* --------------------------------------------------------------------------
 *  acsInitCloth
 *
 *  Build every cloth on a model: lay out the particle and spring arrays inside
 *  top_addr, seed the particles from the SGD's vertex list, and wire up the
 *  spring topology for the cloth's type.
 *
 *  Particles are numbered column-major -- index = column * h + row -- which is
 *  why the "vertical" springs step by 1 and the "horizontal" ones step by h.
 *  The four topologies are:
 *
 *    0        w*(h-1) verticals + (w-1)*h horizontals.  A flat sheet.
 *    4, 5     as 0 but the horizontals wrap (mod p_num) and a third set joins
 *             column 0 to column w-1 -- a tube.
 *    other    the 4/5 tube plus a diagonal set alternating +h+1 / +h-1, which
 *             is what stops it shearing.
 *    1, 2, 6  two independent sheets (front and back) plus one spring per
 *             vertex joining them.  p_num is doubled.
 *
 *  Type 6 additionally records four edge vertices into cdat->rist_vtx -- the
 *  last vertex of columns w-2, w-1, 2w-2 and 2w-1, i.e. both open edges of
 *  both layers.  That is the sleeve attachment.
 * ------------------------------------------------------------------------ */
u_int *acsInitCloth(CLOTH_CTRL *cloth_top, COLLISION_CTRL *collision_ctrl,
                    u_int *mpk_p, u_int *top_addr, int mdl_no, int chodo_flg)
{                                                                       /* 828 */
    CLOTH_DAT  *cdat;
    CLOTH_DAT  *cloth_dat;
    CLOTH      *dat;
    C_PARTICLE *particle;
    SPRING     *spring;
    SGDFILEHEADER *sgd_p;
    float      *vtx;
    u_int       ofs[2];
    u_int       col_num;     /* springs in the vertical (within-column) set   */
    u_int       row_end;     /* index one past the horizontal set             */
    u_int       diag_end;    /* index one past the wrap / back-layer set      */
    u_int       cloth_no;
    u_char      type;
    u_short     w;
    u_short     h;
    u_int       i;
    u_int       j;
    u_int       parity;
    int         n;
    int         rist;

    cloth_no = 0;

    if (chodo_flg == 1)                                                 /* 837 */
    {
        cdat = furn_mdl_dat[mdl_no].cdat;
    }
    else
    {
        cdat = manmdl_dat[mdl_no].cdat;
    }

    if (cdat == (CLOTH_DAT *)0)                                         /* 843 */
    {
        return top_addr;
    }

    dat             = cdat->dat;
    cloth_top->cdat = cdat;                                             /* 847 */

    while (dat != (CLOTH *)0)                                           /* 851 */
    {
        col_num  = 0;
        row_end  = 0;
        diag_end = 0;

        cloth_dat = cloth_top->cdat;
        cloth_no++;

        w = cloth_dat->dat->w;                                          /* 857 */
        h = cloth_dat->dat->h;

        cloth_top->w         = w;
        cloth_top->h         = h;
        cloth_top->reset_flg = 3;                                       /* 859 */

        cloth_top->w_ctrl.dir[0] = 0.0f;                                /* 863 */
        cloth_top->w_ctrl.dir[1] = 0.0f;
        cloth_top->w_ctrl.dir[2] = 0.0f;
        cloth_top->w_ctrl.dir[3] = 0.0f;
        cloth_top->w_ctrl.pow    = 0.0f;
        cloth_top->w_ctrl.cycle  = 0;
        cloth_top->w_ctrl.sta    = 0;
        cloth_top->w_ctrl.strong = 0;
        cloth_top->w_ctrl.weak   = 0;

        /* A chodo is handed its SGD directly; a character's cloth names a file
         * inside the model pak. */
        if (chodo_flg == 0)                                             /* 875 */
        {
            sgd_p = (SGDFILEHEADER *)GetFileInPak(mpk_p, cloth_dat->sgd_id);
        }
        else
        {
            sgd_p = (SGDFILEHEADER *)mpk_p;
        }

        type = dat->type;
        vtx  = (float*)sgd_p->pVectorInfo->aAddress[SVA_UNIQUE].pvVertex.get();                       /* 883 */

        col_num = (u_int)w * (h - 1);

        if (type == 0)                                                  /* 888 */
        {
            cloth_top->p_num      = w * h;
            cloth_top->spring_num = (u_short)(col_num + (w - 1) * h);
        }
        else if ((u_char)(type - 1) < 2 || type == 6)                   /* 891 */
        {
            row_end               = (col_num + (w - 1) * h) & 0xffff;
            diag_end              = (row_end << 1) & 0xffff;
            cloth_top->p_num      = w * h * 2;
            cloth_top->spring_num = (u_short)(diag_end + w * h);
        }
        else if ((u_char)(type - 4) < 2)                                /* 901 */
        {
            row_end               = col_num + (w - 1) * h;
            cloth_top->p_num      = w * h;
            cloth_top->spring_num = (u_short)((h * 2 - 1) * w);
        }
        else
        {
            diag_end              = (u_int)((h * 2 - 1) * w);           /* 907 */
            row_end               = (w - 1) * h + col_num;
            cloth_top->p_num      = w * h;
            cloth_top->spring_num = (u_short)(diag_end + col_num * 2);
        }

        /* Particles first, then springs, each 128-aligned. */
        particle = (C_PARTICLE *)motAlign128(top_addr);                 /* 917 */
        cloth_top->particle = particle;
        cloth_top->spring   = (SPRING *)(particle + cloth_top->p_num);
        top_addr = motAlign128((u_int *)(cloth_top->spring +
                                         cloth_top->spring_num));       /* 919 */

        for (i = 0; i < cloth_top->p_num; i++)                          /* 922 */
        {
            sceVu0CopyVector(particle[i].p, vtx + i * 4);               /* 923 */
            particle[i].p[3] = 1.0f;

            sceVu0CopyVector(particle[i].old, particle[i].p);           /* 925 */
            sceVu0CopyVector(particle[i].org, particle[i].p);           /* 926 */

            particle[i].c_old[3] = 1.0f;
            particle[i].v[0]     = 0.0f;
            particle[i].v[1]     = 0.0f;
            particle[i].v[2]     = 0.0f;
            particle[i].v[3]     = 0.0f;
            particle[i].c_old[0] = 0.0f;
            particle[i].c_old[1] = 0.0f;
            particle[i].c_old[2] = 0.0f;
        }

        sceVu0UnitMatrix(cloth_top->old_w2l);                           /* 935 */

        spring = cloth_top->spring;
        type   = dat->type;

        if (type == 0)                                                  /* 936 */
        {
            for (i = 0; i < cloth_top->spring_num; i++)                 /* 940 */
            {
                if (i < col_num)
                {
                    spring[i].p1 = (u_short)(i % (h - 1) + (i / (h - 1)) * h);
                    spring[i].p2 = spring[i].p1 + 1;                    /* 942 */
                }
                else
                {
                    j = (i - col_num) & 0xffff;
                    spring[i].p1 = (u_short)((j % (w - 1)) * h + j / (w - 1));
                    spring[i].p2 = spring[i].p1 + h;                    /* 947 */
                }
            }
        }
        else if ((u_char)(type - 1) < 2 || type == 6)                   /* 951 */
        {
            /* Front sheet, back sheet, then one spring per vertex tying the
             * two layers together. */
            for (i = 0; i < cloth_top->spring_num; i++)                 /* 1041 */
            {
                if (i < col_num)
                {
                    spring[i].p1 = (u_short)(i % (h - 1) + (i / (h - 1)) * h);
                    spring[i].p2 = spring[i].p1 + 1;
                }
                else if (i < row_end)
                {
                    j = (i - col_num) & 0xffff;
                    spring[i].p1 = (u_short)((j % (w - 1)) * h + j / (w - 1));
                    spring[i].p2 = spring[i].p1 + h;
                }
                else if (i < row_end + col_num)
                {
                    j = i - row_end;
                    spring[i].p1 = (u_short)(j % (h - 1) + (j / (h - 1)) * h + w * h);
                    spring[i].p2 = spring[i].p1 + 1;
                }
                else if (i < diag_end)
                {
                    j = (i - (row_end + col_num)) & 0xffff;
                    spring[i].p1 = (u_short)((j % (w - 1)) * h + j / (w - 1) + w * h);
                    spring[i].p2 = spring[i].p1 + h;
                }
                else
                {
                    spring[i].p1 = (u_short)(i - diag_end);
                    spring[i].p2 = spring[i].p1 + w * h;
                }
            }
        }
        else if ((u_char)(type - 4) < 2)                                /* 962 */
        {
            for (i = 0; i < cloth_top->spring_num; i++)                 /* 963 */
            {
                if (i < col_num)
                {
                    spring[i].p1 = (u_short)(i % (h - 1) + (i / (h - 1)) * h);
                    spring[i].p2 = spring[i].p1 + 1;                    /* 965 */
                }
                else if (i < row_end)
                {
                    j = (i - col_num) & 0xffff;
                    spring[i].p1 = (u_short)(((j % (w - 1)) * h + j / (w - 1)) & 0xffff);
                    spring[i].p2 = (u_short)((spring[i].p1 + h) % cloth_top->p_num);
                                                                        /* 974 */
                }
                else
                {
                    j = i - row_end;
                    spring[i].p1 = (u_short)j;                          /* 971 */
                    spring[i].p2 = (u_short)(j + h * (w - 1));
                }
            }
        }
        else
        {
            for (i = 0; i < cloth_top->spring_num; i++)                 /* 996 */
            {
                if (i < col_num)
                {
                    spring[i].p1 = (u_short)(i % (h - 1) + (i / (h - 1)) * h);
                    spring[i].p2 = spring[i].p1 + 1;                    /* 998 */
                }
                else if (i < row_end)
                {
                    j = (i - col_num) & 0xffff;
                    spring[i].p1 = (u_short)(((j % (w - 1)) * h + j / (w - 1)) & 0xffff);
                    spring[i].p2 = (u_short)((spring[i].p1 + h) % cloth_top->p_num);
                }
                else if (i < diag_end)
                {
                    spring[i].p1 = (u_short)(i - row_end);              /* 1004 */
                    spring[i].p2 = spring[i].p1 + h * (w - 1);
                }
                else
                {
                    /* Alternating diagonals: even springs run +h+1, odd ones
                     * +h-1, so each quad gets both of its braces. */
                    j      = (i - diag_end) & 0xffff;                   /* 1013 */
                    parity = (i - diag_end) & 1;
                    ofs[0] = h + 1;
                    ofs[1] = h - 1;

                    spring[i].p1 = (u_short)((h * ((j % (w * 2u)) >> 1) + parity +
                                              j / (w * 2u)) & 0xffff);
                    spring[i].p2 = (u_short)(((spring[i].p1 + ofs[parity]) & 0xffff) %
                                             cloth_top->p_num);         /* 1019 */
                }
            }
        }

        /* Sleeve attachment points: the last vertex of the two edge columns of
         * each layer.  rist_vtx being NULL is a data error, not fatal. */
        if (dat->type == 6)                                             /* 1053 */
        {
            rist = 0;
            vtx  = (float*)sgd_p->pVectorInfo->aAddress[SVA_UNIQUE].pvVertex.get();

            for (n = 0; n < (int)(w * 2u); n++)                         /* 1056 */
            {
                if (((n >= (int)(w - 2)) && (n < (int)w)) ||
                    (n >= (int)(w * 2u - 2)))                           /* 1058 */
                {
                    if (cloth_top->cdat->rist_vtx == (sceVu0FVECTOR *)0)
                    {
                        printf("Warning : rist_vtx is NULL\n");         /* 1063 */
                    }
                    else
                    {
                        sceVu0CopyVector(cloth_top->cdat->rist_vtx[rist],
                                         vtx + (n * h + h - 1) * 4);    /* 1067 */
                        rist++;
                    }
                }
            }
        }

        /* cloth_no is relative to the table base, not the current entry.  The
         * ROM keeps cdat fixed and uses a separate current-entry register. */
        dat                = cdat[cloth_no].dat;                        /* 1077 */
        cloth_top[1].cdat  = cdat + cloth_no;
        cloth_top++;
    }

    /* One matrix per collision slot, only when the cloth asked for it. */
    if ((cloth_top->cdat->flg != 0) && (collision_ctrl != (COLLISION_CTRL *)0)) /* 1080 */
    {
        for (i = 0; i < 5; i++)
        {
            sceVu0UnitMatrix(collision_ctrl->old_w2c[i]);               /* 1081 */
        }
    }

    return top_addr;                                                    /* 1092 */
}

/* Byte size of the particle+spring buffer acsInitCloth will want for every
 * cloth on this model.  Each cloth contributes p_num C_PARTICLE followed by
 * spring_num SPRING, each run 128-aligned.
 *
 * The topology depends on CLOTH::type:
 *
 *   0        single sheet, structural springs only
 *   1, 2, 6  doubled sheet (front and back), plus one spring joining the layers
 *   4, 5     single sheet, column springs plus a skip-a-row shear set
 *   other    as 4/5 with the horizontal set added back
 *
 * mpk_p is unused -- the ROM takes it and reads manmdl_dat[mdl_no] instead. */
u_int acsGetClothBufSize(u_int *mpk_p, int mdl_no)
{                                                                       /* 1098 */
    CLOTH_DAT *cdat;
    uintptr_t  size;
    u_short    p_num;
    u_short    spring_num;
    u_short    w;
    u_short    h;

    (void)mpk_p;

    cdat = manmdl_dat[mdl_no].cdat;
    size = 0;

    if (cdat == (CLOTH_DAT *)0)                                         /* 1105 */
    {
        return 0;
    }

    while (cdat->dat != (CLOTH *)0)                                     /* 1107 */
    {
        w = cdat->dat->w;                                               /* 1113 */
        h = cdat->dat->h;

        p_num = w * h;                                                  /* 1116 */

        if (cdat->dat->type == 0)                                       /* 1117 */
        {
            spring_num = (w - 1) * h + w * (h - 1);                     /* 1118 */
        }
        else if (((u_char)(cdat->dat->type - 1) < 2) ||
                 (cdat->dat->type == 6))                                /* 1122 */
        {
            p_num      = w * h * 2;                                     /* 1123 */
            spring_num = (w * (h - 1) + (w - 1) * h) * 2 + w * h;
        }
        else if ((u_char)(cdat->dat->type - 4) < 2)                     /* 1127 */
        {
            p_num      = w * h;                                         /* 1129 */
            spring_num = (h * 2 - 1) * w;
        }
        else
        {
            p_num      = w * h;                                         /* 1134 */
            spring_num = (h * 2 - 1) * w + w * (h - 1) * 2;
        }

        /* The EE expresses this as pointer arithmetic from NULL.  Keep it as
         * an integer byte count on the host: dereferencing or doing arithmetic
         * on that synthetic pointer is undefined C++ behaviour. */
        size = (size + 0xfu) & ~(uintptr_t)0xfu;                         /* 1140 */
        size += (uintptr_t)p_num * sizeof(C_PARTICLE);
        size = (size + 0xfu) & ~(uintptr_t)0xfu;                         /* 1141 */
        size += (uintptr_t)spring_num * sizeof(SPRING);
        size = (size + 0xfu) & ~(uintptr_t)0xfu;                         /* 1142 */
        cdat++;                                                         /* 1144 */
    }

    return (u_int)size;                                                  /* 1148 */
}

/* --------------------------------------------------------------------------
 *  acsClothCtrl
 *
 *  Per-frame entry for a character's cloth.  Before stepping, it measures how
 *  far the hip bone moved and how far the model turned since last frame: a
 *  jump of more than 60 units or a turn of more than 0.3 rad is treated as a
 *  teleport and sets reset_flg bit 2, which tells acsMoveCloth not to stretch
 *  the cloth across the gap.
 *
 *  reset_flg bit 0 means "cloth was just (re)built": it is stepped thirty
 *  times to settle, then the bit is cleared.  Same count as a rope's warm-up.
 *
 *  scene_flg negates the scale -- cutscene models are fed a mirrored basis.
 * ------------------------------------------------------------------------ */
void acsClothCtrl(ANI_CTRL *ani_ctrl, u_int *mpk_p, u_int mdl_no, u_char scene_flg)
{                                                                       /* 1155 */
    CLOTH_CTRL     *cloth_top;
    CLOTH_CTRL     *cloth;
    SGDCOORDINATE  *cp;
    COLLISION_DAT  *collision;
    COLLISION_CTRL *collision_ctrl;
    SGDFILEHEADER  *sgd_p;
    float         (*vtx)[4];
    float           sub[4];
    float           pos[4];
    float           rot[4];
    float           move_len;
    float           rot_diff;
    float           scale;
    int             warp;
    u_int           i;

    cloth_top = ani_ctrl->cloth_ctrl;

    if (manmdl_dat[mdl_no].cdat == (CLOTH_DAT *)0)                      /* 1156 */
    {
        return;
    }

    cp = ((SGDFILEHEADER *)&mpk_p[8])->pCoord;                                    /* 1167 */

    /* Hip translation since last frame. */
    sceVu0CopyVector(pos, cp[manmdl_dat[mdl_no].hip_id].matLocalWorld[3]);
                                                                        /* 1174 */
    sceVu0SubVector(sub, pos, ani_ctrl->pbak);                          /* 1178 */
    move_len = g3dxVu0Sqrt(sceVu0InnerProduct(sub, sub));               /* 1180 */
    sceVu0CopyVector(ani_ctrl->pbak, pos);                              /* 1182 */

    /* Facing change: the root's Z axis flattened onto the XZ plane. */
    sceVu0CopyVector(rot, cp->matLocalWorld[2]);                        /* 1184 */
    rot[1] = 0.0f;
    sceVu0Normalize(rot, rot);                                          /* 1190 */
    rot_diff = g3dAcosf(sceVu0InnerProduct(rot, ani_ctrl->rbak));       /* 1192 */
    sceVu0CopyVector(ani_ctrl->rbak, rot);                             /* 1193 */

    warp = (rot_diff > 0.3f);                                           /* 1194 */

    for (cloth = cloth_top; cloth->cdat->dat != (CLOTH *)0; cloth++)    /* 1196 */
    {
        collision      = manmdl_dat[mdl_no].collision;                  /* 1242 */
        collision_ctrl = ani_ctrl->collision_ctrl;
        scale          = manmdl_dat[mdl_no].scale;

        sgd_p = (SGDFILEHEADER*)GetFileInPak(mpk_p, cloth->cdat->sgd_id);               /* 1249 */
        vtx   = sgd_p->pVectorInfo->aAddress[SVA_UNIQUE].pvVertex.get();

        if (scene_flg != 0)                                             /* 1252 */
        {
            scale = -scale;
        }

        if ((cloth->reset_flg & 1) == 0)                                /* 1256 */
        {
            if (warp || (move_len > 60.0f))                             /* 1257 */
            {
                cloth->reset_flg |= 4;
            }

            acsMoveCloth(vtx, cloth, cp, collision_ctrl, collision, scale, 1.0f,
                         manmdl_dat[mdl_no].hip_id);                    /* 1262 */
        }
        else
        {
            for (i = 0; i < 30; i++)                                    /* 1264 */
            {
                acsMoveCloth(vtx, cloth, cp, collision_ctrl, collision, scale,
                             1.0f, manmdl_dat[mdl_no].hip_id);          /* 1265 */
            }

            cloth->reset_flg &= 0xfe;                                   /* 1267 */
        }
    }                                                                   /* 1271 */
}                                                                       /* 1273 */

/* --------------------------------------------------------------------------
 *  acsMoveCloth
 *
 *  One frame of cloth simulation.  The outer loop runs the whole step twice --
 *  gravity, two spring-relaxation passes, re-pin, damp -- and only the second
 *  pass (k == 1) does collision.  Positions live in the bone's local space;
 *  collision is the exception and happens per collision volume, in that
 *  volume's own space.
 *
 *  Two details worth naming:
 *
 *  s_flg marks a particle whose spring was overstretched past 1.3x its rest
 *  length this frame.  Such a particle is moved unconditionally, and any
 *  *other* spring touching it is then forbidden from moving it again -- so one
 *  hard constraint wins over the soft ones instead of them fighting.  At the
 *  end, any spring with both ends flagged has its two velocities averaged,
 *  which is what stops a stretched edge oscillating.
 *
 *  restrict[] marks the particles that were pinned back onto the model's own
 *  vertices this frame (the anchored rows).  Those are the ones NOT written
 *  back to the mesh at the end -- they already match it.
 * ------------------------------------------------------------------------ */
void acsMoveCloth(float (*vtx)[4], CLOTH_CTRL *cloth, SGDCOORDINATE *cp,
                  COLLISION_CTRL *collision_ctrl, COLLISION_DAT *collision,
                  float scale, float collision_scale, u_char hip_id)
{                                                                       /* 1285 */
    fixed_array<sceVu0FMATRIX, 5> c2w;
    fixed_array<sceVu0FMATRIX, 5> w2c;
    C_PARTICLE    *particle;
    SPRING        *spring;
    CLOTH_DAT     *cdat;
    CLOTH         *dat;
    COLLISION_DAT *c;
    C_PARTICLE    *pa;
    C_PARTICLE    *pb;
    float          gravity[4];
    float          trans[4];
    float          l2w[4][4];
    float          w2l[4][4];
    float          old_now_w2w[4][4];
    float          l2w_rist[4][4];
    float          matrix[4][4];
    float          add_vec[4];
    float          d[4];
    float          force[4];
    float          p[4];
    float          v[4];
    float          p1[4];
    float          v1[4];
    float          p2[4];
    float          test[4];
    char           restrict[500];
    float          len;
    u_char         hit;
    u_short        w;
    u_short        h;
    int            b_point;
    int            rist;
    int            idx;
    int            k;
    int            n;
    int            i;
    int            j;

    memset(restrict, 0, sizeof(restrict));                              /* 1288 */

    particle = cloth->particle;
    spring   = cloth->spring;
    cdat     = cloth->cdat;

    if (cdat == (CLOTH_DAT *)0)                                         /* 1296 */
    {
        return;
    }

    dat = cdat->dat;

    sceVu0CopyMatrix(l2w, cp[cdat->bone_id].matLocalWorld);             /* 1298 */

    if (dat->type == 1)                                                 /* 1300 */
    {
        sceVu0CopyMatrix(l2w_rist, cp[cdat->bone_id2].matLocalWorld);   /* 1303 */
    }
    else if (dat->type == 6)                                            /* 1305 */
    {
        /* The sleeve frame is the second bone re-anchored at the first bone's
         * own origin, so the sleeve follows the arm but hangs from the body. */
        sceVu0CopyMatrix(l2w_rist, cp[cdat->bone_id2].matLocalWorld);   /* 1307 */
        sceVu0CopyVector(trans, cp[cdat->bone_id].matCoord[3]);         /* 1308 */
        sceVu0UnitMatrix(matrix);
        sceVu0TransMatrix(matrix, matrix, trans);
        sceVu0MulMatrix(l2w_rist, l2w_rist, matrix);                    /* 1311 */
    }

    gravity[0] = 0.0f;                                                  /* 1312 */
    gravity[1] = dat->gravity * scale;
    gravity[2] = 0.0f;
    gravity[3] = 0.0f;

    motSetInvMatrix(w2l, l2w);                                          /* 1315 */

    if ((cloth->reset_flg & 6) == 0)                                    /* 1338 */
    {
        /* Ordinary frame.  Types 4 and 6 track a bone by hand so the whole
         * sheet is carried along instead of being dragged by its anchors. */
        if ((dat->type == 4) && ((cloth->reset_flg & 1) == 0))          /* 1340 */
        {
            sceVu0SubVector(add_vec, cp[hip_id].matLocalWorld[3],
                            cloth->old_bone_pos);                       /* 1342 */

            for (i = 0; i < (int)(u_short)cloth->p_num; i++)            /* 1343 */
            {
                sceVu0AddVector(particle[i].p,   particle[i].p,   add_vec);
                sceVu0AddVector(particle[i].old, particle[i].old, add_vec);
            }
        }
        else if ((dat->type == 6) && ((cloth->reset_flg & 1) == 0))     /* 1348 */
        {
            /* Only two thirds of the body's movement -- a sleeve lags. */
            sceVu0SubVector(add_vec, cp->matLocalWorld[3], cloth->old_bone_pos);
                                                                        /* 1355 */
            sceVu0ScaleVector(add_vec, add_vec, 0.66f);                 /* 1356 */

            for (i = 0; i < (int)(u_short)cloth->p_num; i++)            /* 1357 */
            {
                sceVu0AddVector(particle[i].p,   particle[i].p,   add_vec);
                sceVu0AddVector(particle[i].old, particle[i].old, add_vec);
            }
        }
    }
    else
    {
        /* Reset: replay the old positions through the bone's movement since
         * last frame so the cloth arrives with the model rather than snapping
         * across the gap. */
        sceVu0MulMatrix(old_now_w2w, l2w, cloth->old_w2l);              /* 1367 */

        for (i = 0; i < (int)(u_short)cloth->p_num; i++)                /* 1369 */
        {
            sceVu0ApplyMatrix(particle[i].p, old_now_w2w, particle[i].old);
            sceVu0CopyVector(particle[i].old, particle[i].p);           /* 1372 */
            particle[i].v[0] = 0.0f;
            particle[i].v[1] = 0.0f;
            particle[i].v[2] = 0.0f;
            particle[i].v[3] = 0.0f;
        }

        /* Bit 1 additionally re-measures every rest length from where the
         * cloth actually is -- a rebuild, not just a warp. */
        if ((cloth->reset_flg & 2) != 0)                                /* 1379 */
        {
            for (i = 0; i < (int)cloth->spring_num; i++)       /* 1380 */
            {
                sceVu0SubVector(d, particle[spring[i].p1].p,
                                   particle[spring[i].p2].p);           /* 1382 */
                spring[i].ldef = g3dxVu0Sqrt(sceVu0InnerProduct(d, d)); /* 1385 */
            }
        }

        cloth->reset_flg &= 0xf9;                                       /* 1391 */
    }

    if (dat->type == 4)                                                 /* 1394 */
    {
        sceVu0CopyVector(cloth->old_bone_pos, cp[hip_id].matLocalWorld[3]);
    }
    else if (dat->type == 6)
    {
        sceVu0CopyVector(cloth->old_bone_pos, cp->matLocalWorld[3]);    /* 1399 */
    }

    sceVu0CopyMatrix(cloth->old_w2l, w2l);                              /* 1401 */

    motClothWind(cloth);                                                /* 1405 */

    for (k = 0; k < 2; k++)                                             /* 1407 */
    {
        for (i = 0; i < (int)cloth->p_num; i++)                /* 1409 */
        {
            sceVu0AddVector(particle[i].v, particle[i].v, gravity);     /* 1410 */
            sceVu0AddVector(particle[i].p, particle[i].p, particle[i].v);
            particle[i].s_flg = 0;                                      /* 1411 */
        }

        /* Two relaxation passes, both walking the spring list backwards.  The
         * ROM writes them out twice rather than looping; the 1.3 threshold is
         * one lit4 per expansion. */
        for (n = 0; n < 2; n++)
        {
            for (i = (int)cloth->spring_num - 1; i >= 0; i--)  /* 1423 */
            {
                pa = particle + spring[i].p1;
                pb = particle + spring[i].p2;

                sceVu0SubVector(d, pa->p, pb->p);                       /* 1426 */
                len = g3dxVu0Sqrt(sceVu0InnerProduct(d, d));            /* 1430 */
                sceVu0ScaleVector(force, d, ((spring[i].ldef - len) * 0.5f) / len);
                                                                        /* 1432 */

                if (spring[i].ldef * 1.3f < len)                        /* 1433 */
                {
                    pa->s_flg = 1;                                      /* 1434 */
                    pb->s_flg = 1;
                    sceVu0AddVector(pa->p, pa->p, force);               /* 1437 */
                    sceVu0SubVector(pb->p, pb->p, force);
                }
                else
                {
                    if (pa->s_flg == 0)                                 /* 1441 */
                    {
                        sceVu0AddVector(pa->p, pa->p, force);
                    }

                    if (pb->s_flg == 0)                                 /* 1444 */
                    {
                        sceVu0SubVector(pb->p, pb->p, force);
                    }
                }
            }
        }

        if ((k == 1) && (cloth->cdat->flg != 0))                        /* 1496 */
        {
            /* Each collision volume gets its own normalised+scaled frame, so
             * the primitives can be tested in unit space. */
            i = 0;

            for (c = collision; c->dat != (void *)0; c++)               /* 1510 */
            {
                if (i > 4)                                              /* 1512 */
                {
                    printf("Warning : Collision data is too large\n");
                    break;
                }

                sceVu0CopyMatrix(c2w[i], cp[c->bone_id].matLocalWorld); /* 1516 */
                sceVu0Normalize(c2w[i][0], c2w[i][0]);
                sceVu0Normalize(c2w[i][1], c2w[i][1]);
                sceVu0Normalize(c2w[i][2], c2w[i][2]);
                sceVu0ScaleVector(c2w[i][0], c2w[i][0], collision_scale);
                sceVu0ScaleVector(c2w[i][1], c2w[i][1], collision_scale);
                sceVu0ScaleVector(c2w[i][2], c2w[i][2], collision_scale);
                motSetInvMatrix(w2c[i], c2w[i]);                        /* 1529 */
                i++;
            }

            for (j = 0; j < (int)(u_short)cloth->p_num; j++)            /* 1533 */
            {
                hit = 0;

                sceVu0CopyVector(p, particle[j].p);                     /* 1536 */
                sceVu0CopyVector(v, particle[j].v);                     /* 1538 */

                i = 0;

                for (c = collision; c->dat != (void *)0; c++)           /* 1539 */
                {
                    if (i > 4)                                          /* 1541 */
                    {
                        printf("Warning : Collision data is too large\n");
                        break;
                    }

                    sceVu0ApplyMatrix(p1, w2c[i], p);                   /* 1545 */
                    sceVu0ApplyMatrix(v1, w2c[i], v);                   /* 1547 */

                    /* The previous frame's position, in this volume's *old*
                     * space -- that is what makes the tube's tunnelling
                     * escape hatch meaningful across a moving bone. */
                    if (collision_ctrl == (COLLISION_CTRL *)0)          /* 1549 */
                    {
                        sceVu0CopyVector(p2, p1);
                    }
                    else
                    {
                        sceVu0ApplyMatrix(p2, collision_ctrl->old_w2c[i],
                                          particle[j].c_old);           /* 1554 */
                    }

                    if (c->type == 1)                                   /* 1558 */
                    {
                        /* No old_c during a reset -- there is no meaningful
                         * previous position to fall back to. */
                        if ((cloth->reset_flg & 1) != 0)
                        {
                            hit = acsCheckCollisionTube((TUBE *)c->dat, p1, v1,
                                                        (float *)0, dat->Ke);
                        }
                        else
                        {
                            hit = acsCheckCollisionTube((TUBE *)c->dat, p1, v1,
                                                        p2, dat->Ke);   /* 1562 */
                        }
                    }
                    else if (c->type == 0)                              /* 1566 */
                    {
                        hit = acsCheckCollisionSphere((SPHERE *)c->dat, p1, v1,
                                                      dat->Ke);
                    }
                    else if (c->type == 2)                              /* 1569 */
                    {
                        /* ofs is the particle index, so successive particles
                         * sit at successive depths off the plane. */
                        hit = acsCheckCollisionPlane((CPLANE *)c->dat, p1, v1,
                                                     dat->Ke, j);       /* 1574 */
                    }

                    if (hit != 0)                                       /* 1577 */
                    {
                        sceVu0ApplyMatrix(particle[j].p, c2w[i], p1);
                    }

                    i++;
                }

                sceVu0CopyVector(particle[j].c_old, particle[j].p);      /* 1580 */
            }

            i = 0;

            for (c = collision; c->dat != (void *)0; c++)               /* 1587 */
            {
                if (collision_ctrl != (COLLISION_CTRL *)0)
                {
                    sceVu0CopyMatrix(collision_ctrl->old_w2c[i], w2c[i]);
                                                                        /* 1589 */
                }
                i++;
            }
        }

        /* Re-pin the anchored rows straight from the model's vertices.  Which
         * rows those are depends on the topology; the pinned indices are the
         * ones that must not be written back at the end. */
        w = cloth->w;
        h = cloth->h;

        if ((dat->type == 0) || ((u_char)(dat->type - 4) < 2))          /* 1598 */
        {
            b_point = cloth->cdat->b_point;

            for (n = 0; n < (int)w; n++)                                /* 1602 */
            {
                for (j = 0; j < b_point; j++)                           /* 1603 */
                {
                    idx = n * h + (h - 1) - j;
                    sceVu0ApplyMatrix(particle[idx].p, l2w, vtx[idx]);  /* 1605 */
                    restrict[idx] = 1;                                  /* 1609 */
                }
            }
        }
        else if ((dat->type == 1) || (dat->type == 6))                  /* 1618 */
        {
            /* Both layers' free edges come from rist_vtx and are NOT marked
             * restricted, so they still get written back -- they are driven by
             * the sleeve bone, not by the sheet. */
            rist = 0;

            for (n = 0; n < (int)(w * 2); n++)                          /* 1619 */
            {
                idx = (n * h + h - 1) & 0xffff;

                if (((n >= (int)(w - 2)) && (n < (int)w)) ||
                    (n >= (int)(w * 2 - 2)))                            /* 1621 */
                {
                    if (cloth->cdat->rist_vtx == (sceVu0FVECTOR *)0)
                    {
                        printf("Warning : rist_vtx is NULL\n");         /* 1631 */
                    }
                    else
                    {
                        sceVu0ApplyMatrix(particle[idx].p, l2w_rist,
                                          cloth->cdat->rist_vtx[rist]); /* 1625 */
                        rist++;
                    }
                }
                else
                {
                    sceVu0ApplyMatrix(particle[idx].p, l2w, vtx[idx]);  /* 1623 */
                    restrict[idx] = 1;
                }
            }
        }
        else if (dat->type == 2)                                        /* 1642 */
        {
            for (n = 0; n < (int)(w * 2); n++)                          /* 1643 */
            {
                idx = (n * h + h - 1) & 0xffff;
                sceVu0ApplyMatrix(particle[idx].p, l2w, vtx[idx]);      /* 1645 */
                restrict[idx] = 1;
            }
        }
        else
        {
            for (n = 0; n < (int)w; n++)                                /* 1652 */
            {
                idx = n * h + (h - 1);
                sceVu0ApplyMatrix(particle[idx].p, l2w, vtx[idx]);      /* 1654 */
                restrict[idx] = 1;
            }
        }

        for (i = 0; i < (int)(u_short)cloth->p_num; i++)                /* 1706 */
        {
            sceVu0SubVector(particle[i].v, particle[i].p, particle[i].old);
                                                                        /* 1707 */
            sceVu0ScaleVector(particle[i].v, particle[i].v, dat->Kd);   /* 1708 */
        }

        /* Both ends overstretched: share one velocity between them. */
        for (i = 0; i < (int)(u_short)cloth->spring_num; i++)           /* 1709 */
        {
            pa = particle + spring[i].p1;
            pb = particle + spring[i].p2;

            if ((pa->s_flg == 1) && (pb->s_flg == 1))
            {
                sceVu0AddVector(test, pa->v, pb->v);
                sceVu0ScaleVector(test, test, 0.5f);
                sceVu0CopyVector(pa->v, test);
                sceVu0CopyVector(pb->v, test);
            }
        }

        for (i = 0; i < (int)(u_short)cloth->p_num; i++)                /* 1728 */
        {
            sceVu0CopyVector(particle[i].old, particle[i].p);
        }
    }

    /* Anything that was not pinned this frame is pushed back into the mesh. */
    for (i = 0; i < (int)(u_short)cloth->p_num; i++)                    /* 1730 */
    {
        if (restrict[i] == 0)
        {
            sceVu0ApplyMatrix(vtx[i], w2l, particle[i].p);
        }
    }
}

/* --------------------------------------------------------------------------
 *  acsChodoSetCloth
 *
 *  Claim a chodo registry slot in bank `id` and build its cloth into
 *  top_addr.  The CLOTH_CTRL itself is placed first (128-aligned), and the
 *  particle/spring buffer follows it, also 128-aligned.  Answers the address
 *  past everything it consumed.
 * ------------------------------------------------------------------------ */
u_int *acsChodoSetCloth(u_int *mpk_p, int mdl_no, int id, u_int *top_addr, int key)
{                                                                       /* 1839 */
    C_ACS_CTRL *entry;
    CLOTH_CTRL *cloth;
    u_int      *buf;
    int         i;

    if ((u_int)id > 2)
    {
        printf("error! map bank id = %d (in acsChodoSetCloth)\n", id);
    }

    for (i = 0; i < CHODO_ACS_MAX; i++)
    {
        if (c_acs_ctrl[id][i].c_cloth_ctrl == nullptr)
        {
            break;
        }
    }

    if (i == CHODO_ACS_MAX)
    {
        printf("CHODO_CLOTH_CTRL buf is FULL!!!!\n");
        return nullptr;
    }

    cloth = (CLOTH_CTRL *)motAlign128(top_addr);

    entry = &c_acs_ctrl[id][i];
    entry->c_cloth_ctrl = cloth;
    entry->addr         = mpk_p;
    entry->mdl_no       = mdl_no;
    entry->key          = key;

    buf = motAlign128((u_int *)(entry->c_cloth_ctrl + 1));

    return acsInitCloth(entry->c_cloth_ctrl, nullptr, mpk_p, buf, mdl_no, 1);
}

/* --------------------------------------------------------------------------
 *  acsChodoClothCtrl
 *
 *  Per-frame entry for every map drape in both banks.  Drapes have no owning
 *  ANI_CTRL, so there is no warp detection here -- only the reset_flg path,
 *  which settles a freshly-built drape over thirty steps as everything else
 *  in this file does.
 *
 *  The inner walk over a registry entry's CLOTH_CTRL chain does not work: the
 *  ROM increments its counter and then loops only while it is zero, so exactly
 *  one cloth per entry is ever stepped.  That is the ROM's code and it is
 *  preserved -- acsClothCtrl's equivalent loop is written correctly, so this
 *  one is a slip rather than a convention.
 * ------------------------------------------------------------------------ */
int acsChodoClothCtrl(void)
{                                                                       /* 1903 */
    fixed_array<COLLISION_DAT, ACS_COLLISION_MAX> collision;
    fixed_array<TUBE, ACS_COLLISION_MAX>          tube;
    CLOTH_CTRL     *cloth;
    SGDCOORDINATE  *cp;
    float         (*vtx)[4];
    SGDFILEHEADER *mpk_p;
    int             mdl_no;
    u_int           no;
    int             id;
    int             i;
    int             n;

    for (id = 0; id < CHODO_BANK_MAX; id++)                             /* 2016 */
    {
        for (i = 0; i < CHODO_ACS_MAX; i++)                             /* 1925 */
        {
            if (c_acs_ctrl[id][i].c_cloth_ctrl == (CLOTH_CTRL *)0)
            {
                break;
            }

            cloth  = c_acs_ctrl[id][i].c_cloth_ctrl;                    /* 1927 */
            mpk_p  = (SGDFILEHEADER*)c_acs_ctrl[id][i].addr;
            mdl_no = c_acs_ctrl[id][i].mdl_no;

            cp = mpk_p->pCoord;                             /* 1931 */

            if (cp == nullptr)                               /* 1934 */
            {
                printf("COORDUNIT is NULL : in acsChodoClothCtrl \n");  /* 1935 */
                printf("mdl no = %d mpk_p = %p\n", mdl_no, mpk_p);
                PRINT_ASSERT("in acsChodoClothCtrl\n");                 /* 1937 */
            }

            no = 0;

            do
            {
                if (cloth->cdat->dat == nullptr)                     /* 1940 */
                {
                    return 0;
                }

                acsChodoMakeCollision(collision.data(), cp, tube.data(), 1.0f);
                                                                        /* 1941 */

                vtx = mpk_p->pVectorInfo->aAddress[SVA_UNIQUE].pvVertex.get();  /* 1945 */

                if ((cloth->reset_flg & 1) == 0)                        /* 1961 */
                {
                    acsMoveCloth(vtx, cloth, cp, nullptr, collision.data(), 1.0f, 1.0f, 0);
                }
                else
                {
                    for (n = 0; n < 30; n++)                            /* 1962 */
                    {
                        acsMoveCloth(vtx, cloth, cp, nullptr, collision.data(), 1.0f, 1.0f, 0);  /* 1964 */
                    }

                    cloth->reset_flg &= 0xfe;                           /* 1965 */
                }

                cloth++;
                no++;
            } while (no == 0);                                          /* 1973 */
        }
    }                                                                   /* 2017 */

    return 1;                                                           /* 2019 */
}
