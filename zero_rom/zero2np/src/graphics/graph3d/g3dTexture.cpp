/* ==========================================================================
 *  g3dTexture.cpp
 *
 *  CTexture: the g3d engine's bindable GS texture resource.  A texture carries
 *  the GS TEX0 / TEX1 register pair (G3DTEXTUREDATA) used when it is bound via
 *  g3dSetTexture, and -- when it owns CPU-side image data -- an sceGsLoadImage
 *  descriptor that PreLoad() replays to upload the image into GS memory.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dTexture.h"
#include "g3ddbg.h"
#include <string.h>            /* memset */
#include <libgraph.h>          /* sceGsExecLoadImage */

/* --------------------------------------------------------------------------
 *  CTexture::CTexture
 *
 *  Tag the resource as a texture, then zero the GS load-image descriptor, the
 *  TEX0/TEX1 data and the source-image pointer/size.
 * ------------------------------------------------------------------------ */
CTexture::CTexture()
{
    m_Type = G3DRTYPE_TEXTURE;

    memset(&m_gsLoadImage, 0, sizeof(m_gsLoadImage));
    memset(&m_TextureData, 0, sizeof(m_TextureData));
    m_iSize  = 0;
    m_pbyData = (unsigned char *)0;
}

/* --------------------------------------------------------------------------
 *  CTexture::Create
 *
 *  Bind the texture to a caller-supplied TEX0/TEX1 register pair.
 * ------------------------------------------------------------------------ */
int CTexture::Create(const G3DTEXTUREDATA *pTextureData)
{
    /* "pTextureData" */
    G3DASSERT(pTextureData, "");

    m_TextureData.l.lTex0 = pTextureData->l.lTex0;
    m_TextureData.l.lTex1 = pTextureData->l.lTex1;

    return 1;
}

/* --------------------------------------------------------------------------
 *  CTexture::PreLoad
 *
 *  If the texture owns CPU-side image data, replay its GS load-image transfer
 *  to upload the image into GS memory.
 * ------------------------------------------------------------------------ */
void CTexture::PreLoad(void)
{
    if (m_pbyData != (unsigned char *)0)
    {
        sceGsExecLoadImage(&m_gsLoadImage, (u_long128 *)m_pbyData);
    }
}
