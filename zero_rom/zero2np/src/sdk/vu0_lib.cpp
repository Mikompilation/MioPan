#include "libvu0.h"
#include "cglm/call/mat4.h"
#include <math.h>

#define POW2(x) ((x) * (x))
#define HALF_PI 1.57079637050628662109e0f
static void _sceVu0ecossin(int sign, float t, float* sin, float* cos)
{
    // sin(x) expansion values seen in the very recent
    // 1979 issue of BYTE magazine, it's an optimal set
    // up to x^9, better than the McLaurin Coefficients
    static const sceVu0FVECTOR S5432 = {
        0.000002601887f,
        -0.00019807414f,
        0.0083330255f,
        -0.16666657f,
    };

    float t2, s, c;

    t2 = t * t; s = t;
    t *= t2; s += S5432[3] * t; // s += k2
    t *= t2; s += S5432[2] * t; // s += z
    t *= t2; s += S5432[1] * t; // s += y
    t *= t2; s += S5432[0] * t; // s += x

    *cos = s;

    c = sqrtf(fabsf(1.0f - POW2(s)));

    if (sign == 0)
    {
        *sin = +c;
    }
    else
    {
        *sin = -c;
    }
}

void sceVu0ScaleVectorXYZ(sceVu0FVECTOR v0, sceVu0FVECTOR v1, float s)
{
    glm_vec3_scale(v1, s, v0);
}

void sceVu0AddVector(sceVu0FVECTOR v0, const sceVu0FVECTOR v1,
                     const sceVu0FVECTOR v2)
{
    for (int i = 0; i < 4; i++)
    {
        v0[i] = v1[i] + v2[i];
    }
}

void sceVu0SubVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2)
{
    glm_vec4_sub(v1, v2, v0);
}

void sceVu0MulVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2)
{
    glm_vec4_mul(v1, v2, v0);
}

void sceVu0CopyVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1)
{
    glm_vec4_copy(v1, v0);
}

void sceVu0Normalize(sceVu0FVECTOR v0, sceVu0FVECTOR v1)
{
    glm_vec3_normalize_to(v1, v0);
    v0[3] = 0.0f;
}

float sceVu0InnerProduct(sceVu0FVECTOR v0, sceVu0FVECTOR v1)
{
    return glm_vec3_dot(v0, v1);
}

void sceVu0OuterProduct(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2)
{
    glm_vec3_cross(v1, v2, v0);
    v0[3] = 0.0f;
}

void sceVu0UnitMatrix(sceVu0FMATRIX m)
{
    glm_mat4_identity(m);
}

void sceVu0TransposeMatrix(sceVu0FMATRIX m0, sceVu0FMATRIX m1)
{
    mat4 out = {0};
    glmc_mat4_transpose_to(m1, out);
    glm_mat4_copy(out, m0);
}

void sceVu0CameraMatrix(sceVu0FMATRIX m, sceVu0FVECTOR p, sceVu0FVECTOR zd,
                        sceVu0FVECTOR yd)
{
    sceVu0FMATRIX m0 = {0};
    sceVu0FVECTOR xd = {0};

    sceVu0UnitMatrix(m0);
    sceVu0OuterProduct(xd, yd, zd);
    sceVu0Normalize(m0[0], xd);
    sceVu0Normalize(m0[2], zd);
    sceVu0OuterProduct(m0[1], m0[2], m0[0]);
    sceVu0TransMatrix(m0, m0, p);
    sceVu0InversMatrix(m, m0);
}

void sceVu0MulMatrix(sceVu0FMATRIX m0, sceVu0FMATRIX m1, sceVu0FMATRIX m2)
{
    mat4 out = {0};
    glmc_mat4_mul(m1, m2, out);

    glm_mat4_copy(out, m0);
}

void sceVu0InversMatrix(sceVu0FMATRIX m0, sceVu0FMATRIX m1)
{
    mat4 out = {0};
    //glm_mat4_inv(m1, out);

    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            out[i][j] = m1[j][i];
        }
    }

    out[3][0] = -(out[0][0]*m1[3][0] + out[1][0]*m1[3][1] + out[2][0]*m1[3][2]);
    out[3][1] = -(out[0][1]*m1[3][0] + out[1][1]*m1[3][1] + out[2][1]*m1[3][2]);
    out[3][2] = -(out[0][2]*m1[3][0] + out[1][2]*m1[3][1] + out[2][2]*m1[3][2]);
    out[3][3] = 1.0f;

    glm_mat4_copy(out, m0);
}

void sceVu0CopyMatrix(sceVu0FMATRIX m0, sceVu0FMATRIX m1)
{
    glm_mat4_copy(m1, m0);
}

void sceVu0ApplyMatrix(sceVu0FVECTOR v0, const sceVu0FMATRIX m,
                       const sceVu0FVECTOR v1)
{
    vec4 out = {0};

    for (int column = 0; column < 4; column++)
    {
        out[column] = m[0][column] * v1[0] + m[1][column] * v1[1] +
                      m[2][column] * v1[2] + m[3][column] * v1[3];
    }
    glm_vec4_copy(out, v0);
}

void sceVu0RotMatrixX(sceVu0FMATRIX m0, sceVu0FMATRIX m1, float rx)
{
    float sin; float cos;
    int sign;
    mat4 out = {0};

    if (rx < 0.0f)
    {
        rx = HALF_PI + rx;
        sign = 1;
    }
    else
    {
        rx = HALF_PI - rx;
        sign = 0;
    }

    _sceVu0ecossin(sign, rx, &sin, &cos);

    for (int i = 0; i < 4; i++)
    {
        out[i][0] = 1.0f * m1[i][0] + 0.0f * m1[i][1] + 0.0f * m1[i][2] + 0.0f * m1[i][3];
        out[i][1] = 0.0f * m1[i][0] +  cos * m1[i][1] + -sin * m1[i][2] + 0.0f * m1[i][3];
        out[i][2] = 0.0f * m1[i][0] +  sin * m1[i][1] +  cos * m1[i][2] + 0.0f * m1[i][3];
        out[i][3] = 0.0f * m1[i][0] + 0.0f * m1[i][1] + 0.0f * m1[i][2] + 1.0f * m1[i][3];
    }

    glm_mat4_copy(out, m0);
}

void sceVu0RotMatrixY(sceVu0FMATRIX m0, sceVu0FMATRIX m1, float ry)
{
    float sin; float cos;
    int sign;

    mat4 out = {0};

    if (ry < 0.0f)
    {
        ry = HALF_PI + ry;
        sign = 1;
    }
    else
    {
        ry = HALF_PI - ry;
        sign = 0;
    }

    _sceVu0ecossin(sign, ry, &sin, &cos);

    for (int i = 0; i < 4; i++)
    {
        out[i][0] =  cos * m1[i][0] + 0.0f * m1[i][1] +  sin * m1[i][2] + 0.0f * m1[i][3];
        out[i][1] = 0.0f * m1[i][0] + 1.0f * m1[i][1] + 0.0f * m1[i][2] + 0.0f * m1[i][3];
        out[i][2] = -sin * m1[i][0] + 0.0f * m1[i][1] +  cos * m1[i][2] + 0.0f * m1[i][3];
        out[i][3] = 0.0f * m1[i][0] + 0.0f * m1[i][1] + 0.0f * m1[i][2] + 1.0f * m1[i][3];
    }

    glm_mat4_copy(out, m0);
}

void sceVu0RotMatrixZ(sceVu0FMATRIX m0, sceVu0FMATRIX m1, float rz)
{
    float sin; float cos;
    int sign;

    mat4 out = {0};

    if (rz < 0.0f)
    {
        rz = HALF_PI + rz;
        sign = 1;
    }
    else
    {
        rz = HALF_PI - rz;
        sign = 0;
    }

    _sceVu0ecossin(sign, rz, &sin, &cos);

    for (int i = 0; i < 4; i++)
    {
        out[i][0] =  cos * m1[i][0] + -sin * m1[i][1] + 0.0f * m1[i][2] + 0.0f * m1[i][3];
        out[i][1] =  sin * m1[i][0] +  cos * m1[i][1] + 0.0f * m1[i][2] + 0.0f * m1[i][3];
        out[i][2] = 0.0f * m1[i][0] + 0.0f * m1[i][1] + 1.0f * m1[i][2] + 0.0f * m1[i][3];
        out[i][3] = 0.0f * m1[i][0] + 0.0f * m1[i][1] + 0.0f * m1[i][2] + 1.0f * m1[i][3];
    }

    glm_mat4_copy(out, m0);
}

void sceVu0RotMatrix(sceVu0FMATRIX m0, sceVu0FMATRIX m1, sceVu0FVECTOR rot)
{
    sceVu0RotMatrixZ(m0, m1, rot[2]);
    sceVu0RotMatrixY(m0, m0, rot[1]);
    sceVu0RotMatrixX(m0, m0, rot[0]);
}

void sceVu0TransMatrix(sceVu0FMATRIX m0, sceVu0FMATRIX m1, sceVu0FVECTOR tv)
{
    glm_mat4_copy(m1, m0);
    glm_vec3_add(tv, m0[3], m0[3]);
}

#define FLT_TO_FIX4(_val) ((int)((_val) * 16.0f))
void sceVu0RotTransPersN(sceVu0IVECTOR *v0, sceVu0FMATRIX m0, sceVu0FVECTOR *v1, int n, int mode)
{
    int i;

    vec4 tmp = {0};
    // Always runs at least once, but code assumes n is non-zero so a for it is
    // Also technically clobbers vf4:vf7, but I don't think the game relies on that behavior
    for (i = 0; i < n; i++)
    {
        tmp[0] = m0[0][0] * v1[i][0] + m0[1][0] * v1[i][1] + m0[2][0] * v1[i][2] + m0[3][0] * v1[i][3];
        tmp[1] = m0[0][1] * v1[i][0] + m0[1][1] * v1[i][1] + m0[2][1] * v1[i][2] + m0[3][1] * v1[i][3];
        tmp[2] = m0[0][2] * v1[i][0] + m0[1][2] * v1[i][1] + m0[2][2] * v1[i][2] + m0[3][2] * v1[i][3];
        tmp[3] = m0[0][3] * v1[i][0] + m0[1][3] * v1[i][1] + m0[2][3] * v1[i][2] + m0[3][3] * v1[i][3];

        tmp[0] *= 1.0f / tmp[3];
        tmp[1] *= 1.0f / tmp[3];
        tmp[2] *= 1.0f / tmp[3];

        /* Sony's routine always performs vftoi4 on XY.  Mode zero keeps the
         * same conversion for ZW; non-zero mode overwrites only ZW with
         * vftoi0.  GS screen coordinates therefore remain 12.4 in either
         * mode. */
        v0[i][0] = FLT_TO_FIX4(tmp[0]);
        v0[i][1] = FLT_TO_FIX4(tmp[1]);
        if (mode == 0)
        {
            v0[i][2] = FLT_TO_FIX4(tmp[2]);
            v0[i][3] = FLT_TO_FIX4(tmp[3]);
        }
        else
        {
            v0[i][2] = (int)(tmp[2]);
            v0[i][3] = (int)(tmp[3]);
        }
    }
}

void sceVu0RotTransPersNF(sceVu0FVECTOR *v0, sceVu0FMATRIX m0, sceVu0FVECTOR *v1, int n, int mode)
{
    int i;

    vec4 tmp = {0};
    // Always runs at least once, but code assumes n is non-zero so a for it is
    // Also technically clobbers vf4:vf7, but I don't think the game relies on that behavior
    for (i = 0; i < n; i++)
    {
        tmp[0] = m0[0][0] * v1[i][0] + m0[1][0] * v1[i][1] + m0[2][0] * v1[i][2] + m0[3][0] * v1[i][3];
        tmp[1] = m0[0][1] * v1[i][0] + m0[1][1] * v1[i][1] + m0[2][1] * v1[i][2] + m0[3][1] * v1[i][3];
        tmp[2] = m0[0][2] * v1[i][0] + m0[1][2] * v1[i][1] + m0[2][2] * v1[i][2] + m0[3][2] * v1[i][3];
        tmp[3] = m0[0][3] * v1[i][0] + m0[1][3] * v1[i][1] + m0[2][3] * v1[i][2] + m0[3][3] * v1[i][3];

        tmp[0] *= 1.0f / tmp[3];
        tmp[1] *= 1.0f / tmp[3];
        tmp[2] *= 1.0f / tmp[3];

        v0[i][0] = (tmp[0]);
        v0[i][1] = (tmp[1]);
        v0[i][2] = (tmp[2]);
        v0[i][3] = (tmp[3]);
    }
}

void sceVu0RotTransPers(sceVu0IVECTOR v0, sceVu0FMATRIX m0, sceVu0FVECTOR v1, int mode)
{
    vec4 tmp = {0};

    tmp[0] = m0[0][0] * v1[0] + m0[1][0] * v1[1] + m0[2][0] * v1[2] + m0[3][0] * v1[3];
    tmp[1] = m0[0][1] * v1[0] + m0[1][1] * v1[1] + m0[2][1] * v1[2] + m0[3][1] * v1[3];
    tmp[2] = m0[0][2] * v1[0] + m0[1][2] * v1[1] + m0[2][2] * v1[2] + m0[3][2] * v1[3];
    tmp[3] = m0[0][3] * v1[0] + m0[1][3] * v1[1] + m0[2][3] * v1[2] + m0[3][3] * v1[3];

    tmp[0] *= 1.0f / tmp[3];
    tmp[1] *= 1.0f / tmp[3];
    tmp[2] *= 1.0f / tmp[3];

    v0[0] = FLT_TO_FIX4(tmp[0]);
    v0[1] = FLT_TO_FIX4(tmp[1]);
    if (mode == 0)
    {
        v0[2] = FLT_TO_FIX4(tmp[2]);
        v0[3] = FLT_TO_FIX4(tmp[3]);
    }
    else
    {
        v0[2] = (int)(tmp[2]);
        v0[3] = (int)(tmp[3]);
    }
}

void sceVu0RotTransPersF(sceVu0FVECTOR v0, sceVu0FMATRIX m0, sceVu0FVECTOR v1, int mode)
{
    vec4 tmp = {0};

    tmp[0] = m0[0][0] * v1[0] + m0[1][0] * v1[1] + m0[2][0] * v1[2] + m0[3][0] * v1[3];
    tmp[1] = m0[0][1] * v1[0] + m0[1][1] * v1[1] + m0[2][1] * v1[2] + m0[3][1] * v1[3];
    tmp[2] = m0[0][2] * v1[0] + m0[1][2] * v1[1] + m0[2][2] * v1[2] + m0[3][2] * v1[3];
    tmp[3] = m0[0][3] * v1[0] + m0[1][3] * v1[1] + m0[2][3] * v1[2] + m0[3][3] * v1[3];

    tmp[0] *= 1.0f / tmp[3];
    tmp[1] *= 1.0f / tmp[3];
    tmp[2] *= 1.0f / tmp[3];

    if (mode == 0)
    {
        v0[0] = (tmp[0]);
        v0[1] = (tmp[1]);
        v0[2] = (tmp[2]);
        v0[3] = (tmp[3]);
    }
    else
    {
        v0[0] = (tmp[0]);
        v0[1] = (tmp[1]);
        v0[2] = (tmp[2]);
        v0[3] = (tmp[3]);
    }
}

void sceVu0ScaleVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, float s)
{
    glm_vec4_scale(v1, s, v0);
}

void sceVu0DivVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, float q)
{
    if (q == 0.0f)
    {
        glm_vec4_zero(v0);
        return;
    }

    glm_vec4_divs(v1, q, v0);
}

void sceVu0DivVectorXYZ(sceVu0FVECTOR v0, sceVu0FVECTOR v1, float q)
{
    if (q == 0.0f)
    {
        glm_vec3_zero(v0);
        return;
    }

    glm_vec3_divs(v1, q, v0);
}

void sceVu0ClampVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, float min, float max)
{
    for (int i = 0; i < 4; i++)
    {
        if (v1[i] < min)
        {
            v0[i] = min;
        }
        else if (v1[i] > max)
        {
            v0[i] = max;
        }
        else
        {
            v0[i] = v1[i];
        }
    }
}

/* v0 = r*v1 + (1 - r)*v2, all four components.
 *
 *   vaddw.x  vf7, vf0, vf0    -> 1.0
 *   vsub.x   vf8, vf7, vf6    -> 1 - r
 *   vmulax.xyzw  ACC, vf4, vf6
 *   vmaddx.xyzw  vf9, vf5, vf8
 *
 * Note the weighting: r selects v1, not v2. */
void sceVu0InterVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2,
                       float r)
{
    float inv_r = 1.0f - r;

    v0[0] = v1[0] * r + v2[0] * inv_r;
    v0[1] = v1[1] * r + v2[1] * inv_r;
    v0[2] = v1[2] * r + v2[2] * inv_r;
    v0[3] = v1[3] * r + v2[3] * inv_r;
}

/* As sceVu0InterVector, but the VU op is masked to .xyz and a preceding
 * vmove.w copies v1's W through untouched rather than blending it. */
void sceVu0InterVectorXYZ(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2,
                          float r)
{
    float inv_r = 1.0f - r;
    float w     = v1[3];   /* captured first: v0 may alias v1 or v2 */

    v0[0] = v1[0] * r + v2[0] * inv_r;
    v0[1] = v1[1] * r + v2[1] * inv_r;
    v0[2] = v1[2] * r + v2[2] * inv_r;
    v0[3] = w;
}

/* ==========================================================================
 *  Port shims
 *
 *  None of these exist as symbols in the prototype -- they were inline COP2
 *  there and only became calls in this port, so they are shims rather than
 *  reconstructions of a real SCE entry point.
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  Skinning blend
 *
 *  The two bone matrices are latched by sceVu0LoadMatrix0/1, then each vertex
 *  pair is blended against them.  Source layout is two quadwords per vertex:
 *  v1 is the bone-0 position with the weight in w, v1 + 4 the bone-1 position.
 * ------------------------------------------------------------------------ */
static sceVu0FMATRIX s_blend_matrix0 = {
    {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f},
};

static sceVu0FMATRIX s_blend_matrix1 = {
    {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f},
};

void sceVu0LoadMatrix0(sceVu0FMATRIX m0)
{
    sceVu0CopyMatrix(s_blend_matrix0, m0);
}

void sceVu0LoadMatrix1(sceVu0FMATRIX m0)
{
    sceVu0CopyMatrix(s_blend_matrix1, m0);
}

void sceVu0BlendVertex(sceVu0FVECTOR v0, sceVu0FVECTOR v1)
{
    const float *v2 = v1 + 4;
    float        w0 = v1[3];
    float        w1 = 1.0f - w0;
    int          c;

    for (c = 0; c < 3; c++)
    {
        float p0 = v1[0] * s_blend_matrix0[0][c] + v1[1] * s_blend_matrix0[1][c] +
                   v1[2] * s_blend_matrix0[2][c] + s_blend_matrix0[3][c];
        float p1 = v2[0] * s_blend_matrix1[0][c] + v2[1] * s_blend_matrix1[1][c] +
                   v2[2] * s_blend_matrix1[2][c] + s_blend_matrix1[3][c];

        v0[c] = p0 * w0 + p1 * w1;
    }
    v0[3] = 1.0f;
}

/* As above but rotation only, scaled by the inverse mean scale that
   _CalcWeightedLocalWorldMatrix parks in matRet[3][3]. */
void sceVu0BlendNormal(sceVu0FVECTOR v0, sceVu0FVECTOR v1)
{
    const float *v2 = v1 + 4;
    float        inv_scale = s_blend_matrix0[3][3];
    float        w0 = v1[3] * inv_scale;
    float        w1 = (1.0f - v1[3]) * inv_scale;
    int          c;

    for (c = 0; c < 3; c++)
    {
        float n0 = v1[0] * s_blend_matrix0[0][c] + v1[1] * s_blend_matrix0[1][c] +
                   v1[2] * s_blend_matrix0[2][c];
        float n1 = v2[0] * s_blend_matrix1[0][c] + v2[1] * s_blend_matrix1[1][c] +
                   v2[2] * s_blend_matrix1[2][c];

        v0[c] = n0 * w0 + n1 * w1;
    }
    v0[3] = 1.0f;
}
