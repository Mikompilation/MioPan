#ifndef MIOPAN_GRAPH3D_H
#define MIOPAN_GRAPH3D_H

#include "graphics/graph3d/gra3dTypes.h"
#include "graphics/graph3d/sgd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hand gra3dApplyCamera()'s own matrices to the renderer: `view` is the
 * world->view matrix it just built, and the projection is read from
 * camera->matViewClipObject.  There is no separate host camera. */
void MioPan_Graph3dApplyCamera(const GRA3DCAMERA *camera,
                               const float (*view)[4]);
void MioPan_Graph3dDrawPresetMesh(SGDFILEHEADER *owner,
                                  SGDPROCUNITHEADER *vuvn,
                                  SGDPROCUNITHEADER *mesh,
                                  const float *local_world);
void MioPan_Graph3dDrawRuntimeMesh(SGDFILEHEADER *owner,
                                   SGDPROCUNITHEADER *vuvn,
                                   SGDPROCUNITHEADER *mesh,
                                   const float *local_world);
void MioPan_Graph3dDrawRuntimeMeshPost(SGDFILEHEADER *owner,
                                      SGDPROCUNITHEADER *vuvn,
                                      SGDPROCUNITHEADER *mesh,
                                      const float *local_world,
                                      const DVECTOR *post_vuvn);
void MioPan_Graph3dUploadGsImage(SGDPROCUNITHEADER *image_unit);
/* Called by the SGD remapper before an allocation is reused or unmapped.
 * Retires everything held for the model: the post-skin cache, its resident
 * mesh and its resolved textures. */
void MioPan_Graph3dInvalidateSkinCache(SGDFILEHEADER *owner);
/* Called by gra3dChangeST() before it moves a model's UVs.  Retires the
 * post-skin cache and the resident mesh, and keeps the resolved textures --
 * they are named by TEX0, which it does not touch. */
void MioPan_Graph3dNotifyUVChange(SGDFILEHEADER *owner);

/* The TRI2 half of resolved textures, for gra3dLoadTRI2FileToVRAM().
 *
 * TextureUploadBound() is asked before a TRI2 header chain is mirrored into GS
 * memory: non-zero means it belongs to the model being walked and that model's
 * textures are already resolved for the current colour mode, so the host
 * mirror is skipped.  NoteTextureUpload() is told after one that did go
 * through, which is what lets the model's first draw know its texels are in GS
 * memory and resolve them. */
int  MioPan_Graph3dTextureUploadBound(const void *tri2_head);
void MioPan_Graph3dNoteTextureUpload(const void *tri2_head);

/* The TIM2 half, for mdlwork.c: characters, ghosts and items keep their
 * textures as TIM2 pictures in their own pack and send them before every draw.
 *
 * Bracket each send with Begin/End.  `source` is the pack -- a character's
 * MPK (GetFileInPak(mdl_p, 0)) or an item's pk2 -- and `kind` says which;
 * `monotone` is non-zero when the monotone CLUT set is being sent.  Begin
 * returns non-zero when that pack's textures are already resolved for the
 * mode, in which case the GS uploads inside the bracket are dropped; the EE
 * side runs unchanged either way.
 *
 * NotifyTex0Rebase() retires a character pack's resolved textures when
 * MpkAddTexOffset() re-bases its TEX0s, which happens around event scenes. */
enum
{
    MIOPAN_TIM2_SOURCE_CHARACTER = 0,
    MIOPAN_TIM2_SOURCE_ITEM = 1
};
int  MioPan_Graph3dBeginTim2Upload(void *source, int kind, int monotone);
void MioPan_Graph3dEndTim2Upload(void);
void MioPan_Graph3dNotifyTex0Rebase(void *source);

#ifdef __cplusplus
}
#endif

#endif
