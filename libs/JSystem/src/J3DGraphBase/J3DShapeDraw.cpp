#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/J3DGraphBase/J3DShapeDraw.h"
#include "JSystem/JKernel/JKRHeap.h"
#include <cstring>
#include <stdint.h>
#include <gx.h>
#ifdef TARGET_PC
#include <cstdio>
extern int g_pc_verbose;
extern "C" u32 VIGetRetraceCount(void);
#endif

u32 J3DShapeDraw::countVertex(u32 stride) {
    u32 count = 0;
    u8* dlStart = (u8*)getDisplayList();

    for (u8* dl = dlStart; (dl - dlStart) < getDisplayListSize();) {
        u8 cmd = *(u8*)dl;
        dl++;
        if (cmd != GX_TRIANGLEFAN && cmd != GX_TRIANGLESTRIP)
            break;
        int vtxNum = *((u16*)(dl));
        dl += 2;
        count += vtxNum;
        dl = (u8*)dl + stride * vtxNum;
    }

    return count;
}

void J3DShapeDraw::addTexMtxIndexInDL(u32 stride, u32 attrOffs, u32 valueBase) {
    u32 byteNum = countVertex(stride);
    u32 oldSize = mDisplayListSize;
    u32 newSize = ALIGN_NEXT(oldSize + byteNum, 0x20);
    u8* newDLStart = new (0x20) u8[newSize];
    u8* oldDLStart = (u8*)mDisplayList;
    u8* oldDL = oldDLStart;
    u8* newDL = newDLStart;

    for (; (oldDL - oldDLStart) < mDisplayListSize;) {
        // Copy command
        u8 cmd = *(u8*)oldDL;
        oldDL++;
        *newDL++ = cmd;

        if (cmd != GX_TRIANGLEFAN && cmd != GX_TRIANGLESTRIP)
            break;

        // Copy count
        int vtxNum = *(u16*)oldDL;
        oldDL += 2;
        *(u16*)newDL = vtxNum;
        newDL += 2;

        for (int i = 0; i < vtxNum; i++) {
            u8* oldDLVtx = &oldDL[stride * i];
            u8 pnmtxidx = *oldDLVtx;
            memcpy(newDL, oldDLVtx, (int)attrOffs);
            newDL += attrOffs;
            *newDL++ = valueBase + pnmtxidx;
            memcpy(newDL, oldDLVtx + attrOffs, stride - attrOffs);
            newDL += (stride - attrOffs);
        }

        oldDL = (u8*)oldDL + stride * vtxNum;
    }

    u32 realSize = ALIGN_NEXT((uintptr_t)newDL - (uintptr_t)newDLStart, 0x20);
    for (; (newDL - newDLStart) < newSize; newDL++)
        *newDL = 0;

    mDisplayListSize = realSize;
    mDisplayList = newDLStart;
    DCStoreRange(newDLStart, mDisplayListSize);
}

J3DShapeDraw::J3DShapeDraw(const u8* displayList, u32 displayListSize) {
    mDisplayList = (void*)displayList;
    mDisplayListSize = displayListSize;
}

void J3DShapeDraw::draw() const {
#ifdef TARGET_PC
    static int s_shape_dl_log = 0;
    if (s_shape_dl_log < 60 && mDisplayList && mDisplayListSize > 0) {
        const u8* p = (const u8*)mDisplayList;
        fprintf(stderr,
                "[SHAPE-DL] size=%u first8=%02x%02x%02x%02x %02x%02x%02x%02x\n",
                mDisplayListSize,
                p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        s_shape_dl_log++;
    }
#endif
    GXCallDisplayList(mDisplayList, mDisplayListSize);
}

J3DShapeDraw::~J3DShapeDraw() {}
