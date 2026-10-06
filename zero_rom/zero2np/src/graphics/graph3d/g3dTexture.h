/* ==========================================================================
 *  g3dTexture.h
 *
 *  The g3d texture resource.  CTexture wraps a G3DTEXTUREDATA (the GS TEX0 /
 *  TEX1 register pair) plus an optional sceGsLoadImage descriptor and source
 *  image pointer, so a texture can both be bound (its TEX0/TEX1 pushed to the
 *  GS by g3dSetTexture) and -- when it owns CPU-side image data -- uploaded to
 *  GS memory on demand via PreLoad().
 *
 *  CTexture derives from IG3DResource, the common base for the engine's GS
 *  resources (textures / vertex buffers / index buffers); the base tags the
 *  concrete resource type and supplies the virtual PreLoad() hook.
 *
 *  Implemented in g3dTexture.cpp.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DTEXTURE_H
#define _G3DTEXTURE_H

#include "eetypes.h"
#include "sce_gs.h"             /* sceGsLoadImage */
#include "gra3dTypes.h"         /* G3DTEXTUREDATA, G3DRESOURCETYPE */

/* --------------------------------------------------------------------------
 *  IG3DResource
 *
 *  Common base for the engine's GS resources.  Holds the concrete resource
 *  type tag and exposes the virtual PreLoad() upload hook.
 * ------------------------------------------------------------------------ */
class IG3DResource
{
protected:
    G3DRESOURCETYPE m_Type;     /* 0x0 */

public:
    G3DRESOURCETYPE GetType(void)
    {
        return m_Type;
    }

    virtual void PreLoad(void)
    {
    }
};

/* --------------------------------------------------------------------------
 *  CTexture
 *
 *  A bindable GS texture.  m_TextureData holds the TEX0/TEX1 register values;
 *  m_gsLoadImage / m_pbyData describe an optional CPU-side image to upload.
 * ------------------------------------------------------------------------ */
class CTexture : public IG3DResource
{
private:
    sceGsLoadImage  m_gsLoadImage;      /* 0x10 */
    G3DTEXTUREDATA  m_TextureData;      /* 0x70 */
    unsigned char  *m_pbyData;          /* 0x80 */
    int             m_iSize;            /* 0x84 */

public:
    CTexture();

    int             Create(const G3DTEXTUREDATA *pTextureData);

    G3DTEXTUREDATA &GetTextureDataRef(void)
    {
        return m_TextureData;
    }

    unsigned char  *GetImage(void)
    {
        return m_pbyData;
    }

    int             GetSize(void)
    {
        return m_iSize;
    }

    virtual void    PreLoad(void);
};

#endif /* _G3DTEXTURE_H */
