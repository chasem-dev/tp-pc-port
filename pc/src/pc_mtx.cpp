/* pc_mtx.cpp - Matrix math replacing PPC paired-singles assembly */
#include "pc_platform.h"
#include <math.h>
#include <stdint.h>
#define BAD_PTR(p) ((uintptr_t)(p) < 0x10000)

extern "C" {

typedef float Mtx[3][4];
typedef float Mtx44[4][4];
typedef float (*MtxP)[4];
typedef struct { float x, y, z; } Vec;
typedef struct { float x, y, z, w; } Quaternion;

void PSMTXIdentity(Mtx m) {
    m[0][0]=1; m[0][1]=0; m[0][2]=0; m[0][3]=0;
    m[1][0]=0; m[1][1]=1; m[1][2]=0; m[1][3]=0;
    m[2][0]=0; m[2][1]=0; m[2][2]=1; m[2][3]=0;
}

void PSMTXCopy(const Mtx src, Mtx dst) {
    if (BAD_PTR(src) || BAD_PTR(dst)) return;
    memcpy(dst, src, sizeof(Mtx));
}

void PSMTXConcat(const Mtx a, const Mtx b, Mtx ab) {
    if (BAD_PTR(a) || BAD_PTR(b) || BAD_PTR(ab)) { if (!BAD_PTR(ab)) PSMTXIdentity(ab); return; }
    Mtx tmp;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0]*b[0][j] + a[i][1]*b[1][j] + a[i][2]*b[2][j];
            if (j == 3) tmp[i][j] += a[i][3];
        }
    }
    memcpy(ab, tmp, sizeof(Mtx));
}

u32 PSMTXInverse(const Mtx src, Mtx inv) {
    if ((uintptr_t)src < 0x10000 || (uintptr_t)inv < 0x10000) { if ((uintptr_t)inv >= 0x10000) PSMTXIdentity(inv); return 0; }
    float det = src[0][0]*(src[1][1]*src[2][2]-src[1][2]*src[2][1])
              - src[0][1]*(src[1][0]*src[2][2]-src[1][2]*src[2][0])
              + src[0][2]*(src[1][0]*src[2][1]-src[1][1]*src[2][0]);
    if (fabsf(det) < 1e-12f) return 0;
    float invdet = 1.0f / det;

    inv[0][0] =  (src[1][1]*src[2][2]-src[1][2]*src[2][1]) * invdet;
    inv[0][1] = -(src[0][1]*src[2][2]-src[0][2]*src[2][1]) * invdet;
    inv[0][2] =  (src[0][1]*src[1][2]-src[0][2]*src[1][1]) * invdet;
    inv[1][0] = -(src[1][0]*src[2][2]-src[1][2]*src[2][0]) * invdet;
    inv[1][1] =  (src[0][0]*src[2][2]-src[0][2]*src[2][0]) * invdet;
    inv[1][2] = -(src[0][0]*src[1][2]-src[0][2]*src[1][0]) * invdet;
    inv[2][0] =  (src[1][0]*src[2][1]-src[1][1]*src[2][0]) * invdet;
    inv[2][1] = -(src[0][0]*src[2][1]-src[0][1]*src[2][0]) * invdet;
    inv[2][2] =  (src[0][0]*src[1][1]-src[0][1]*src[1][0]) * invdet;

    inv[0][3] = -(inv[0][0]*src[0][3] + inv[0][1]*src[1][3] + inv[0][2]*src[2][3]);
    inv[1][3] = -(inv[1][0]*src[0][3] + inv[1][1]*src[1][3] + inv[1][2]*src[2][3]);
    inv[2][3] = -(inv[2][0]*src[0][3] + inv[2][1]*src[1][3] + inv[2][2]*src[2][3]);
    return 1;
}

void PSMTXTranspose(const Mtx src, Mtx xpose) {
    Mtx tmp;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            tmp[i][j] = src[j][i];
    tmp[0][3] = tmp[1][3] = tmp[2][3] = 0;
    memcpy(xpose, tmp, sizeof(Mtx));
}

void PSMTXInvXpose(const Mtx src, Mtx xpose) {
    Mtx inv;
    if (PSMTXInverse(src, inv)) {
        PSMTXTranspose(inv, xpose);
    } else {
        PSMTXIdentity(xpose);
    }
}

void PSMTXMultVec(const Mtx m, const Vec* src, Vec* dst) {
    if (BAD_PTR(m) || BAD_PTR(src) || BAD_PTR(dst)) { if (!BAD_PTR(dst)) { dst->x=dst->y=dst->z=0; } return; }
    float x = m[0][0]*src->x + m[0][1]*src->y + m[0][2]*src->z + m[0][3];
    float y = m[1][0]*src->x + m[1][1]*src->y + m[1][2]*src->z + m[1][3];
    float z = m[2][0]*src->x + m[2][1]*src->y + m[2][2]*src->z + m[2][3];
    dst->x = x; dst->y = y; dst->z = z;
}

void PSMTXMultVecSR(const Mtx m, const Vec* src, Vec* dst) {
    if (BAD_PTR(m) || BAD_PTR(src) || BAD_PTR(dst)) { if (!BAD_PTR(dst)) { dst->x=dst->y=dst->z=0; } return; }
    float x = m[0][0]*src->x + m[0][1]*src->y + m[0][2]*src->z;
    float y = m[1][0]*src->x + m[1][1]*src->y + m[1][2]*src->z;
    float z = m[2][0]*src->x + m[2][1]*src->y + m[2][2]*src->z;
    dst->x = x; dst->y = y; dst->z = z;
}

void PSMTXMultVecArray(const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count) {
    for (u32 i = 0; i < count; i++) PSMTXMultVec(m, &srcBase[i], &dstBase[i]);
}

void PSMTXMultVecArraySR(const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count) {
    for (u32 i = 0; i < count; i++) PSMTXMultVecSR(m, &srcBase[i], &dstBase[i]);
}

void PSMTXTrans(Mtx m, float x, float y, float z) {
    PSMTXIdentity(m);
    m[0][3] = x; m[1][3] = y; m[2][3] = z;
}

void PSMTXTransApply(const Mtx src, Mtx dst, float x, float y, float z) {
    if (src != dst) PSMTXCopy(src, dst);
    dst[0][3] += x; dst[1][3] += y; dst[2][3] += z;
}

void PSMTXScale(Mtx m, float x, float y, float z) {
    PSMTXIdentity(m);
    m[0][0] = x; m[1][1] = y; m[2][2] = z;
}

void PSMTXScaleApply(const Mtx src, Mtx dst, float x, float y, float z) {
    for (int j = 0; j < 4; j++) {
        dst[0][j] = src[0][j] * x;
        dst[1][j] = src[1][j] * y;
        dst[2][j] = src[2][j] * z;
    }
}

void PSMTXRotRad(Mtx m, char axis, float rad) {
    float s = sinf(rad), c = cosf(rad);
    PSMTXIdentity(m);
    switch (axis) {
        case 'x': case 'X':
            m[1][1] = c; m[1][2] = -s;
            m[2][1] = s; m[2][2] = c; break;
        case 'y': case 'Y':
            m[0][0] = c; m[0][2] = s;
            m[2][0] = -s; m[2][2] = c; break;
        case 'z': case 'Z':
            m[0][0] = c; m[0][1] = -s;
            m[1][0] = s; m[1][1] = c; break;
    }
}

void PSMTXRotTrig(Mtx m, char axis, float sinA, float cosA) {
    PSMTXIdentity(m);
    switch (axis) {
        case 'x': case 'X':
            m[1][1] = cosA; m[1][2] = -sinA;
            m[2][1] = sinA; m[2][2] = cosA; break;
        case 'y': case 'Y':
            m[0][0] = cosA; m[0][2] = sinA;
            m[2][0] = -sinA; m[2][2] = cosA; break;
        case 'z': case 'Z':
            m[0][0] = cosA; m[0][1] = -sinA;
            m[1][0] = sinA; m[1][1] = cosA; break;
    }
}

void PSMTXQuat(Mtx m, const Quaternion* q) {
    float xx = q->x*q->x, yy = q->y*q->y, zz = q->z*q->z;
    float xy = q->x*q->y, xz = q->x*q->z, yz = q->y*q->z;
    float wx = q->w*q->x, wy = q->w*q->y, wz = q->w*q->z;
    m[0][0]=1-2*(yy+zz); m[0][1]=2*(xy-wz);   m[0][2]=2*(xz+wy);   m[0][3]=0;
    m[1][0]=2*(xy+wz);   m[1][1]=1-2*(xx+zz); m[1][2]=2*(yz-wx);   m[1][3]=0;
    m[2][0]=2*(xz-wy);   m[2][1]=2*(yz+wx);   m[2][2]=1-2*(xx+yy); m[2][3]=0;
}

/* --- Projection matrices --- */
void C_MTXPerspective(Mtx44 m, float fovY, float aspect, float n, float f) {
    float angle = fovY * 0.5f * PC_PIf / 180.0f;
    float cot = cosf(angle) / sinf(angle);
    memset(m, 0, sizeof(Mtx44));
    m[0][0] = cot / aspect;
    m[1][1] = cot;
    m[2][2] = -(n + f) / (f - n);
    m[2][3] = -(2 * n * f) / (f - n);
    m[3][2] = -1;
}

void C_MTXFrustum(Mtx44 m, float t, float b, float l, float r, float n, float f) {
    memset(m, 0, sizeof(Mtx44));
    m[0][0] = (2*n) / (r-l);
    m[0][2] = (r+l) / (r-l);
    m[1][1] = (2*n) / (t-b);
    m[1][2] = (t+b) / (t-b);
    m[2][2] = -(f+n) / (f-n);
    m[2][3] = -(2*f*n) / (f-n);
    m[3][2] = -1;
}

void C_MTXOrtho(Mtx44 m, float t, float b, float l, float r, float n, float f) {
    memset(m, 0, sizeof(Mtx44));
    m[0][0] = 2.0f / (r-l);
    m[0][3] = -(r+l) / (r-l);
    m[1][1] = 2.0f / (t-b);
    m[1][3] = -(t+b) / (t-b);
    m[2][2] = -1.0f / (f-n);
    m[2][3] = -n / (f-n);
    m[3][3] = 1;
}

void C_MTXLookAt(Mtx m, const Vec* camPos, const Vec* camUp, const Vec* target) {
    Vec f, s, u;
    f.x = target->x - camPos->x;
    f.y = target->y - camPos->y;
    f.z = target->z - camPos->z;
    float len = sqrtf(f.x*f.x + f.y*f.y + f.z*f.z);
    if (len > 0) { f.x /= len; f.y /= len; f.z /= len; }

    s.x = f.y*camUp->z - f.z*camUp->y;
    s.y = f.z*camUp->x - f.x*camUp->z;
    s.z = f.x*camUp->y - f.y*camUp->x;
    len = sqrtf(s.x*s.x + s.y*s.y + s.z*s.z);
    if (len > 0) { s.x /= len; s.y /= len; s.z /= len; }

    u.x = s.y*f.z - s.z*f.y;
    u.y = s.z*f.x - s.x*f.z;
    u.z = s.x*f.y - s.y*f.x;

    m[0][0]=s.x; m[0][1]=s.y; m[0][2]=s.z; m[0][3]=-(s.x*camPos->x+s.y*camPos->y+s.z*camPos->z);
    m[1][0]=u.x; m[1][1]=u.y; m[1][2]=u.z; m[1][3]=-(u.x*camPos->x+u.y*camPos->y+u.z*camPos->z);
    m[2][0]=-f.x; m[2][1]=-f.y; m[2][2]=-f.z; m[2][3]=(f.x*camPos->x+f.y*camPos->y+f.z*camPos->z);
}

void C_MTXLightPerspective(Mtx m, float fovY, float aspect, float scaleS, float scaleT,
                            float transS, float transT) {
    float angle = fovY * 0.5f * PC_PIf / 180.0f;
    float cot = cosf(angle) / sinf(angle);
    memset(m, 0, sizeof(Mtx));
    m[0][0] = (cot / aspect) * scaleS;
    m[0][2] = -transS;
    m[1][1] = cot * scaleT;
    m[1][2] = -transT;
    m[2][2] = -1.0f;
}

void C_MTXLightFrustum(Mtx m, float t, float b, float l, float r, float n,
                        float scaleS, float scaleT, float transS, float transT) {
    (void)t; (void)b; (void)l; (void)r; (void)n;
    (void)scaleS; (void)scaleT; (void)transS; (void)transT;
    PSMTXIdentity(m);
}

void C_MTXLightOrtho(Mtx m, float t, float b, float l, float r,
                      float scaleS, float scaleT, float transS, float transT) {
    memset(m, 0, sizeof(Mtx));
    m[0][0] = 2.0f / (r-l) * scaleS;
    m[0][3] = -(r+l) / (r-l) * scaleS + transS;
    m[1][1] = 2.0f / (t-b) * scaleT;
    m[1][3] = -(t+b) / (t-b) * scaleT + transT;
    m[2][2] = 0;
    m[2][3] = 1;
}

/* Vector utilities */
void PSVECAdd(const Vec* a, const Vec* b, Vec* ab) {
    ab->x = a->x + b->x; ab->y = a->y + b->y; ab->z = a->z + b->z;
}
void PSVECSubtract(const Vec* a, const Vec* b, Vec* ab) {
    ab->x = a->x - b->x; ab->y = a->y - b->y; ab->z = a->z - b->z;
}
void PSVECScale(const Vec* src, Vec* dst, float scale) {
    dst->x = src->x * scale; dst->y = src->y * scale; dst->z = src->z * scale;
}
float PSVECDotProduct(const Vec* a, const Vec* b) {
    return a->x*b->x + a->y*b->y + a->z*b->z;
}
void PSVECCrossProduct(const Vec* a, const Vec* b, Vec* axb) {
    Vec tmp;
    tmp.x = a->y*b->z - a->z*b->y;
    tmp.y = a->z*b->x - a->x*b->z;
    tmp.z = a->x*b->y - a->y*b->x;
    *axb = tmp;
}
float PSVECMag(const Vec* v) { return sqrtf(v->x*v->x + v->y*v->y + v->z*v->z); }
float PSVECSquareMag(const Vec* v) { return v->x*v->x + v->y*v->y + v->z*v->z; }
void PSVECNormalize(const Vec* src, Vec* dst) {
    float m = PSVECMag(src);
    if (m > 0) { float inv = 1.0f/m; dst->x = src->x*inv; dst->y = src->y*inv; dst->z = src->z*inv; }
    else { *dst = *src; }
}
float PSVECSquareDistance(const Vec* a, const Vec* b) {
    float dx = a->x-b->x, dy = a->y-b->y, dz = a->z-b->z;
    return dx*dx + dy*dy + dz*dz;
}
float PSVECDistance(const Vec* a, const Vec* b) { return sqrtf(PSVECSquareDistance(a, b)); }

/* Aliases */
void C_VECAdd(const Vec* a, const Vec* b, Vec* ab) { PSVECAdd(a, b, ab); }
void C_VECSubtract(const Vec* a, const Vec* b, Vec* ab) { PSVECSubtract(a, b, ab); }
void C_VECScale(const Vec* src, Vec* dst, float scale) { PSVECScale(src, dst, scale); }
float C_VECDotProduct(const Vec* a, const Vec* b) { return PSVECDotProduct(a, b); }
void C_VECCrossProduct(const Vec* a, const Vec* b, Vec* axb) { PSVECCrossProduct(a, b, axb); }
float C_VECMag(const Vec* v) { return PSVECMag(v); }
float C_VECSquareMag(const Vec* v) { return PSVECSquareMag(v); }
void C_VECNormalize(const Vec* src, Vec* dst) { PSVECNormalize(src, dst); }
float C_VECSquareDistance(const Vec* a, const Vec* b) { return PSVECSquareDistance(a, b); }
float C_VECDistance(const Vec* a, const Vec* b) { return PSVECDistance(a, b); }

void C_VECHalfAngle(const Vec* a, const Vec* b, Vec* half) {
    Vec na, nb;
    PSVECNormalize(a, &na);
    PSVECNormalize(b, &nb);
    PSVECAdd(&na, &nb, half);
    PSVECNormalize(half, half);
}

void C_VECReflect(const Vec* src, const Vec* normal, Vec* dst) {
    float d = 2.0f * PSVECDotProduct(src, normal);
    dst->x = src->x - d * normal->x;
    dst->y = src->y - d * normal->y;
    dst->z = src->z - d * normal->z;
}

/* Additional matrix/quaternion functions */
void PSMTXRotAxisRad(Mtx m, const Vec* axis, f32 rad) {
    float s = sinf(rad), c = cosf(rad), t = 1.0f - c;
    float x = axis->x, y = axis->y, z = axis->z;
    float len = sqrtf(x*x + y*y + z*z);
    if (len > 0) { x /= len; y /= len; z /= len; }
    m[0][0]=t*x*x+c;   m[0][1]=t*x*y-s*z; m[0][2]=t*x*z+s*y; m[0][3]=0;
    m[1][0]=t*x*y+s*z; m[1][1]=t*y*y+c;   m[1][2]=t*y*z-s*x; m[1][3]=0;
    m[2][0]=t*x*z-s*y; m[2][1]=t*y*z+s*x; m[2][2]=t*z*z+c;   m[2][3]=0;
}
void C_MTXRotAxisRad(Mtx m, const Vec* axis, f32 rad) { PSMTXRotAxisRad(m, axis, rad); }

void PSQUATMultiply(const Quaternion* p, const Quaternion* q, Quaternion* pq) {
    Quaternion tmp;
    tmp.w = p->w*q->w - p->x*q->x - p->y*q->y - p->z*q->z;
    tmp.x = p->w*q->x + p->x*q->w + p->y*q->z - p->z*q->y;
    tmp.y = p->w*q->y - p->x*q->z + p->y*q->w + p->z*q->x;
    tmp.z = p->w*q->z + p->x*q->y - p->y*q->x + p->z*q->w;
    *pq = tmp;
}

void C_QUATRotAxisRad(Quaternion* q, const Vec* axis, f32 rad) {
    f32 half = rad * 0.5f;
    f32 s = sinf(half);
    f32 len = sqrtf(axis->x*axis->x + axis->y*axis->y + axis->z*axis->z);
    if (len > 0) {
        q->x = axis->x / len * s; q->y = axis->y / len * s; q->z = axis->z / len * s;
    } else { q->x = q->y = q->z = 0; }
    q->w = cosf(half);
}

void C_QUATSlerp(const Quaternion* p, const Quaternion* q, Quaternion* r, f32 t) {
    f32 dot = p->x*q->x + p->y*q->y + p->z*q->z + p->w*q->w;
    Quaternion q2 = *q;
    if (dot < 0) { dot = -dot; q2.x=-q2.x; q2.y=-q2.y; q2.z=-q2.z; q2.w=-q2.w; }
    if (dot > 0.9999f) {
        r->x = p->x + t*(q2.x-p->x); r->y = p->y + t*(q2.y-p->y);
        r->z = p->z + t*(q2.z-p->z); r->w = p->w + t*(q2.w-p->w);
    } else {
        f32 theta = acosf(dot);
        f32 sn = sinf(theta);
        f32 wa = sinf((1.0f-t)*theta) / sn;
        f32 wb = sinf(t*theta) / sn;
        r->x = wa*p->x + wb*q2.x; r->y = wa*p->y + wb*q2.y;
        r->z = wa*p->z + wb*q2.z; r->w = wa*p->w + wb*q2.w;
    }
}

} /* extern "C" */
