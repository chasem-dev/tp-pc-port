#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/J3DGraphBase/J3DTexture.h"
#include "JSystem/J3DAssert.h"

void J3DTexture::loadGX(u16 idx, GXTexMapID texMapID) const {
    J3D_ASSERT_RANGE(29, idx < mNum);
    {
        static int s_entry = 0;
        if (s_entry < 20 && mNum > 10) {
            fprintf(stderr, "[loadGX-MODEL] idx=%d map=%d num=%d this=%p fmt=%d %dx%d\n",
                    idx, texMapID, mNum, (void*)this,
                    getResTIMG(idx)->format, getResTIMG(idx)->width, getResTIMG(idx)->height);
            s_entry++;
        }
    }

    ResTIMG* timg = getResTIMG(idx);
#ifdef TARGET_PC
    /* On 64-bit, imageOffset may have overflowed during setResTIMG.
     * Use the original pointer to the source ResTIMG for image data access. */
    const ResTIMG* srcTimg = getOrigResTIMG(idx);
    u8* imgData = ((u8*)srcTimg) + srcTimg->imageOffset;
    u8* palData = ((u8*)srcTimg) + srcTimg->paletteOffset;
    static int s_tl = 0;
    if (s_tl++ < 50 && timg->format != 14 && timg->format != 6) { /* skip framebuffer textures */
        /* Scan from image data start to find non-zero pixels */
        int firstNZ = -1;
        u32 imgOff = srcTimg->imageOffset;
        for (int i = 0; i < 0x40000 && imgData + i < ((u8*)srcTimg) + 0x200000; i++) {
            if (imgData[i] != 0) { firstNZ = i; break; }
        }
        fprintf(stderr, "[TEX] loadGX: idx=%d fmt=%d %dx%d imgOff=0x%x src=%p img=%p imgFirstNZ=%d\n",
                idx, timg->format, timg->width, timg->height, imgOff,
                (void*)srcTimg, (void*)imgData, firstNZ);
        if (firstNZ >= 0) {
            fprintf(stderr, "[TEX]   imgData[%d]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    firstNZ, imgData[firstNZ], imgData[firstNZ+1], imgData[firstNZ+2], imgData[firstNZ+3],
                    imgData[firstNZ+4], imgData[firstNZ+5], imgData[firstNZ+6], imgData[firstNZ+7]);
        }
    }
#else
    u8* imgData = ((u8*)timg) + timg->imageOffset;
    u8* palData = ((u8*)timg) + timg->paletteOffset;
#endif
    GXTexObj texObj;
    GXTlutObj tlutObj;

    if (!timg->indexTexture) {
        GXInitTexObj(&texObj, imgData, timg->width, timg->height,
                     (GXTexFmt)timg->format, (GXTexWrapMode)timg->wrapS, (GXTexWrapMode)timg->wrapT,
                     timg->mipmapEnabled);
    } else {
        GXInitTexObjCI(&texObj, imgData, timg->width, timg->height,
                       (GXCITexFmt)timg->format, (GXTexWrapMode)timg->wrapS,
                       (GXTexWrapMode)timg->wrapT, timg->mipmapEnabled, (u32)texMapID);
        GXInitTlutObj(&tlutObj, palData, (GXTlutFmt)timg->colorFormat,
                      timg->numColors);
        GXLoadTlut(&tlutObj, texMapID);
    }

    const f32 kLODClampScale = 1.0f / 8.0f;
    const f32 kLODBiasScale = 1.0f / 100.0f;
    GXInitTexObjLOD(&texObj, (GXTexFilter)timg->minFilter, (GXTexFilter)timg->magFilter,
                    timg->minLOD * kLODClampScale, timg->maxLOD * kLODClampScale,
                    timg->LODBias * kLODBiasScale, timg->biasClamp, timg->doEdgeLOD,
                    (GXAnisotropy)timg->maxAnisotropy);
    GXLoadTexObj(&texObj, texMapID);
}

void J3DTexture::entryNum(u16 num) {
    J3D_ASSERT_NONZEROARG(79, num != 0);

    mNum = num;
    mpRes = new ResTIMG[num];
    J3D_ASSERT_ALLOCMEM(83, mpRes != NULL);

    for (int i = 0; i < mNum; i++) {
        mpRes[i].paletteOffset = 0;
        mpRes[i].imageOffset = 0;
    }
}

void J3DTexture::addResTIMG(u16 newNum, const ResTIMG* newRes) {
    if (newNum == 0)
        return;

    J3D_ASSERT_NULLPTR(105, newRes != NULL);

    u16 oldNum = mNum;
    ResTIMG* oldRes = mpRes;

    entryNum(mNum + newNum);

    for (u16 i = 0; i < oldNum; i++) {
        setResTIMG(i, oldRes[i]);
    }

    for (u16 i = oldNum; i < mNum; i++) {
        setResTIMG(i, newRes[i]);
    }
}
