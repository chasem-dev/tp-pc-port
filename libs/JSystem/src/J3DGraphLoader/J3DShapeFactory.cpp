#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/J3DGraphLoader/J3DShapeFactory.h"
#include "JSystem/J3DGraphBase/J3DShape.h"
#include "JSystem/J3DGraphBase/J3DShapeMtx.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JSupport/JSupport.h"
#include <os.h>
#include "global.h"

#ifdef TARGET_PC
static inline bool pc_is_plausible_vcd_pair(u32 attr, u32 type) {
    return (attr == GX_VA_NULL || attr <= GX_VA_TEX7) && type <= GX_INDEX16;
}

static int pc_score_vcd_stream(const u8* base, u32 regionBytes, u32 off) {
    if (base == NULL || off + 8 > regionBytes) {
        return -1;
    }
    const u8* p = base + off;
    const u8* end = base + regionBytes;
    int score = 0;
    for (int i = 0; i < 32 && p + 8 <= end; i++, p += 8) {
        u32 attr = *(const u32*)(p + 0);
        u32 type = *(const u32*)(p + 4);
        if (!pc_is_plausible_vcd_pair(attr, type)) {
            break;
        }
        score += 2;
        if (attr == GX_VA_NULL) {
            score += 20;
            break;
        }
    }
    return score;
}

static u32 pc_pick_vcd_offset(const J3DShapeFactory* self, u16 idx) {
    const u8* base = (const u8*)self->mVtxDescList;
    if (base == NULL) {
        return (u32)idx;
    }

    const u8* end = NULL;
    if ((const u8*)self->mMtxTable > base) {
        end = (const u8*)self->mMtxTable;
    } else if ((const u8*)self->mDisplayListData > base) {
        end = (const u8*)self->mDisplayListData;
    }

    if (end == NULL || end <= base) {
        return (u32)idx;
    }
    u32 regionBytes = (u32)(end - base);

    u16 idxSwap = (u16)((idx >> 8) | (idx << 8));
    u32 candidates[32];
    int candCount = 0;
    candidates[candCount++] = idx;
    candidates[candCount++] = idxSwap;
    candidates[candCount++] = (u32)idx * 2u;
    candidates[candCount++] = (u32)idxSwap * 2u;
    candidates[candCount++] = (u32)idx * 4u;
    candidates[candCount++] = (u32)idxSwap * 4u;
    candidates[candCount++] = (u32)idx * 8u;
    candidates[candCount++] = (u32)idxSwap * 8u;

    if ((u32)idx * 2u + 2u <= regionBytes) {
        u16 offTable = *(const u16*)(base + (u32)idx * 2u);
        u16 offTableSwap = (u16)((offTable >> 8) | (offTable << 8));
        if (candCount < 32) candidates[candCount++] = offTable;
        if (candCount < 32) candidates[candCount++] = offTableSwap;
        if (candCount < 32) candidates[candCount++] = (u32)offTable * 2u;
        if (candCount < 32) candidates[candCount++] = (u32)offTableSwap * 2u;
        if (candCount < 32) candidates[candCount++] = (u32)offTable * 4u;
        if (candCount < 32) candidates[candCount++] = (u32)offTableSwap * 4u;
        if (candCount < 32) candidates[candCount++] = (u32)offTable * 8u;
        if (candCount < 32) candidates[candCount++] = (u32)offTableSwap * 8u;
    }

    int bestScore = -1;
    u32 bestOff = (u32)idx;
    for (int i = 0; i < candCount; i++) {
        u32 offBase = candidates[i];
        for (int delta = -16; delta <= 16; delta++) {
            int64_t off64 = (int64_t)offBase + delta;
            if (off64 < 0) {
                continue;
            }
            u32 off = (u32)off64;
            int score = pc_score_vcd_stream(base, regionBytes, off);
            if (score > bestScore) {
                bestScore = score;
                bestOff = off;
            }
        }
    }

    if (bestScore >= 8) {
        return bestOff;
    }
    return (u32)idx;
}
#endif

J3DShapeFactory::J3DShapeFactory(J3DShapeBlock const& block) {
    mShapeInitData = JSUConvertOffsetToPtr<J3DShapeInitData>(&block, (uintptr_t)block.mpShapeInitData);
    mIndexTable = JSUConvertOffsetToPtr<u16>(&block, (uintptr_t)block.mpIndexTable);
    mVtxDescList = JSUConvertOffsetToPtr<GXVtxDescList>(&block, (uintptr_t)block.mpVtxDescList);
    mMtxTable = JSUConvertOffsetToPtr<u16>(&block, (uintptr_t)block.mpMtxTable);
    mDisplayListData = JSUConvertOffsetToPtr<u8>(&block, (uintptr_t)block.mpDisplayListData);
    mMtxInitData = JSUConvertOffsetToPtr<J3DShapeMtxInitData>(&block, (uintptr_t)block.mpMtxInitData);
    mDrawInitData = JSUConvertOffsetToPtr<J3DShapeDrawInitData>(&block, (uintptr_t)block.mpDrawInitData);
    mVcdVatCmdBuffer = NULL;
}

GXVtxDescList* J3DShapeFactory::getVtxDescList(int no) const {
#ifdef TARGET_PC
    const u16 rawIdx = mShapeInitData[mIndexTable[no]].mVtxDescListIndex;
    const u32 off = pc_pick_vcd_offset(this, rawIdx);
    return (GXVtxDescList*)((u8*)mVtxDescList + off);
#else
    return (GXVtxDescList*)((u8*)mVtxDescList + mShapeInitData[mIndexTable[no]].mVtxDescListIndex);
#endif
}

#ifdef TARGET_PC
static inline u8 pc_j3d_shape_mtx_type(const J3DShapeInitData& shapeInitData) {
    u8 type = shapeInitData.mShapeMtxType;
    if (type > 3) {
        type &= 0x03;
    }
    return type;
}

static inline u16 pc_j3d_shape_draw_index(const J3DShapeInitData& shapeInitData) {
    if (shapeInitData.mDrawInitDataIndex == 0xFFFF) {
        return *(const u16*)((const u8*)&shapeInitData + 0x0A);
    }
    return shapeInitData.mDrawInitDataIndex;
}
#endif

J3DShape* J3DShapeFactory::create(int no, u32 flag, GXVtxDescList* vtxDesc) {
    J3DShape* shape = new J3DShape;
    J3D_ASSERT_ALLOCMEM(67, shape);
    shape->mMtxGroupNum = getMtxGroupNum(no);
    shape->mRadius = getRadius(no);
    shape->mVtxDesc = getVtxDescList(no);
    shape->mShapeMtx = new J3DShapeMtx*[shape->mMtxGroupNum];
    J3D_ASSERT_ALLOCMEM(74, shape->mShapeMtx);
    shape->mShapeDraw = new J3DShapeDraw*[shape->mMtxGroupNum];
    J3D_ASSERT_ALLOCMEM(76, shape->mShapeDraw);
    shape->mMin = getMin(no);
    shape->mMax = getMax(no);
    shape->mVcdVatCmd = mVcdVatCmdBuffer + no * J3DShape::kVcdVatDLSize;

    for (s32 i = 0; i < shape->mMtxGroupNum; i++) {
        shape->mShapeMtx[i] = newShapeMtx(flag, no, i);
        shape->mShapeDraw[i] = newShapeDraw(no, i);
    }

    shape->mIndex = no;
    return shape;
}

enum {
    J3DMdlDataFlag_ConcatView = 0x10,
};

enum {
    J3DShapeMtxType_Mtx = 0x00,
    J3DShapeMtxType_BBoard = 0x01,
    J3DShapeMtxType_YBBoard = 0x02,
    J3DShapeMtxType_Multi = 0x03,
};

J3DShapeMtx* J3DShapeFactory::newShapeMtx(u32 flag, int shapeNo, int mtxGroupNo) const {
    J3DShapeMtx* ret = NULL;
    const J3DShapeInitData& shapeInitData = mShapeInitData[mIndexTable[shapeNo]];
    const J3DShapeMtxInitData& mtxInitData =
        (&mMtxInitData[shapeInitData.mMtxInitDataIndex])[mtxGroupNo];
    u8 shapeMtxType = shapeInitData.mShapeMtxType;
#ifdef TARGET_PC
    shapeMtxType = pc_j3d_shape_mtx_type(shapeInitData);
#endif

    u32 mtxLoadType = getMdlDataFlag_MtxLoadType(flag);
    switch (mtxLoadType) {
    case J3DMdlDataFlag_ConcatView:
        switch (shapeMtxType) {
        case J3DShapeMtxType_Mtx:
            ret = new J3DShapeMtxConcatView(mtxInitData.mUseMtxIndex);
            break;
        case J3DShapeMtxType_BBoard:
            ret = new J3DShapeMtxBBoardConcatView(mtxInitData.mUseMtxIndex);
            break;
        case J3DShapeMtxType_YBBoard:
            ret = new J3DShapeMtxYBBoardConcatView(mtxInitData.mUseMtxIndex);
            break;
        case J3DShapeMtxType_Multi:
            ret = new J3DShapeMtxMultiConcatView(mtxInitData.mUseMtxIndex, mtxInitData.mUseMtxCount,
                                                 &mMtxTable[mtxInitData.mFirstUseMtxIndex]);
            break;
        default:
#ifdef TARGET_PC
            static int s_badShapeMtxLogCount = 0;
            if (s_badShapeMtxLogCount < 32) {
                fprintf(stderr,
                        "[SHP] invalid mtx type: shape=%d idx=%u grp=%d type=%u groupNum=%u vtxDescIdx=0x%x"
                        " mtxInitIdx=%u drawInitIdx=%u useMtxIdx=%u useMtxCount=%u firstUseMtxIdx=0x%x"
                        " flag=0x%x\n",
                        shapeNo, mIndexTable[shapeNo], mtxGroupNo, shapeInitData.mShapeMtxType,
                        shapeInitData.mMtxGroupNum, shapeInitData.mVtxDescListIndex,
                        shapeInitData.mMtxInitDataIndex, shapeInitData.mDrawInitDataIndex,
                        mtxInitData.mUseMtxIndex, mtxInitData.mUseMtxCount,
                        mtxInitData.mFirstUseMtxIndex, flag);
                fflush(stderr);
                s_badShapeMtxLogCount++;
            }
#endif
            OSReport("WRONG SHAPE MATRIX TYPE (J3DModelInit.cpp)\n");
            break;
        }
        break;

    case 0:
    default:
        switch (shapeMtxType) {
        case J3DShapeMtxType_Mtx:
        case J3DShapeMtxType_BBoard:
        case J3DShapeMtxType_YBBoard:
            ret = new J3DShapeMtx(mtxInitData.mUseMtxIndex);
            break;
        case J3DShapeMtxType_Multi:
            ret = new J3DShapeMtxMulti(mtxInitData.mUseMtxIndex, mtxInitData.mUseMtxCount,
                                       &mMtxTable[mtxInitData.mFirstUseMtxIndex]);
            break;
        default:
#ifdef TARGET_PC
            static int s_badShapeMtxLogCount = 0;
            if (s_badShapeMtxLogCount < 32) {
                fprintf(stderr,
                        "[SHP] invalid mtx type: shape=%d idx=%u grp=%d type=%u groupNum=%u vtxDescIdx=0x%x"
                        " mtxInitIdx=%u drawInitIdx=%u useMtxIdx=%u useMtxCount=%u firstUseMtxIdx=0x%x"
                        " flag=0x%x\n",
                        shapeNo, mIndexTable[shapeNo], mtxGroupNo, shapeInitData.mShapeMtxType,
                        shapeInitData.mMtxGroupNum, shapeInitData.mVtxDescListIndex,
                        shapeInitData.mMtxInitDataIndex, shapeInitData.mDrawInitDataIndex,
                        mtxInitData.mUseMtxIndex, mtxInitData.mUseMtxCount,
                        mtxInitData.mFirstUseMtxIndex, flag);
                fflush(stderr);
                s_badShapeMtxLogCount++;
            }
#endif
            OSReport("WRONG SHAPE MATRIX TYPE (J3DModelInit.cpp)\n");
            break;
        }

        break;
    }

    J3D_ASSERT_ALLOCMEM(167, ret);
    return ret;
}

J3DShapeDraw* J3DShapeFactory::newShapeDraw(int shapeNo, int mtxGroupNo) const {
    J3DShapeDraw* shapeDraw = NULL;
    const J3DShapeInitData& shapeInitData = mShapeInitData[mIndexTable[shapeNo]];
    u16 drawInitIndex = shapeInitData.mDrawInitDataIndex;
#ifdef TARGET_PC
    drawInitIndex = pc_j3d_shape_draw_index(shapeInitData);
#endif
    const J3DShapeDrawInitData& drawInitData =
        (&mDrawInitData[drawInitIndex])[mtxGroupNo];

#ifdef TARGET_PC
    /* Fix unswapped DrawInitData — the SHP1 byte-swap may miss entries if
     * the totalMtxGrp count is wrong. Detect and fix big-endian values. */
    u32 dlIndex = drawInitData.mDisplayListIndex;
    u32 dlSize = drawInitData.mDisplayListSize;
    if (dlSize > 0x100000 || dlIndex > 0x100000) {
        /* Likely unswapped big-endian — byte-swap both fields */
        u32 swSize = __builtin_bswap32(dlSize);
        u32 swIdx = __builtin_bswap32(dlIndex);
        static int s_fix_log = 0;
        if (s_fix_log++ < 5) {
            fprintf(stderr, "[SHP-FIX] shape=%d grp=%d: fixing unswapped DrawInitData (size 0x%x→0x%x, idx 0x%x→0x%x)\n",
                    shapeNo, mtxGroupNo, dlSize, swSize, dlIndex, swIdx);
        }
        dlSize = swSize;
        dlIndex = swIdx;
    }
    shapeDraw = new J3DShapeDraw(&mDisplayListData[dlIndex], dlSize);
#else
    shapeDraw = new J3DShapeDraw(&mDisplayListData[drawInitData.mDisplayListIndex], drawInitData.mDisplayListSize);
#endif
    J3D_ASSERT_ALLOCMEM(193, shapeDraw);
    return shapeDraw;
}

void J3DShapeFactory::allocVcdVatCmdBuffer(u32 count) {
    mVcdVatCmdBuffer = new (0x20) u8[J3DShape::kVcdVatDLSize * count];
    J3D_ASSERT_ALLOCMEM(211, mVcdVatCmdBuffer);
    for (u32 i = 0; i < (J3DShape::kVcdVatDLSize * count) / 4; i++)
        ((u32*)mVcdVatCmdBuffer)[i] = 0;
}

s32 J3DShapeFactory::calcSize(int shapeNo, u32 flag) {
    s32 size = 0;

    s32 mtxGroupNo = getMtxGroupNum(shapeNo);
    size += sizeof(J3DShape);
    size += mtxGroupNo * 4;
    size += mtxGroupNo * 4;

    for (u32 i = 0; i < mtxGroupNo; i++) {
        size += calcSizeShapeMtx(flag, shapeNo, i);
        size += 0x0C;
    }

    return size;
}

s32 J3DShapeFactory::calcSizeVcdVatCmdBuffer(u32 count) {
    return ALIGN_NEXT(count * J3DShape::kVcdVatDLSize, 0x20);
}

s32 J3DShapeFactory::calcSizeShapeMtx(u32 flag, int shapeNo, int mtxGroupNo) const {
    int local_18 = 0;
    const J3DShapeInitData& shapeInitData = mShapeInitData[mIndexTable[shapeNo]];
    J3DShapeMtxInitData& mtxInitData = (&mMtxInitData[shapeInitData.mMtxInitDataIndex])[mtxGroupNo];
    u32 ret = 0;
    u8 shapeMtxType = shapeInitData.mShapeMtxType;
#ifdef TARGET_PC
    shapeMtxType = pc_j3d_shape_mtx_type(shapeInitData);
#endif

    u32 mtxLoadType = getMdlDataFlag_MtxLoadType(flag);
    switch (mtxLoadType) {
    case J3DMdlDataFlag_ConcatView:
        switch (shapeMtxType) {
        case J3DShapeMtxType_Mtx:
            ret += sizeof(J3DShapeMtxConcatView);
            break;
        case J3DShapeMtxType_BBoard:
            ret += sizeof(J3DShapeMtxBBoardConcatView);
            break;
        case J3DShapeMtxType_YBBoard:
            ret += sizeof(J3DShapeMtxYBBoardConcatView);
            break;
        case J3DShapeMtxType_Multi:
            ret += sizeof(J3DShapeMtxMultiConcatView);
            break;
        default:
            OSReport("WRONG SHAPE MATRIX TYPE (J3DModelInit.cpp)\n");
        }
        break;

    case 0:
    default:
        switch (shapeMtxType) {
        case J3DShapeMtxType_Mtx:
        case J3DShapeMtxType_BBoard:
        case J3DShapeMtxType_YBBoard:
            ret += 0x08;
            break;
        case J3DShapeMtxType_Multi:
            ret += sizeof(J3DShapeMtxMultiConcatView);
            break;
        default:
            OSReport("WRONG SHAPE MATRIX TYPE (J3DModelInit.cpp)\n");
        }
        break;
    }

    return ret;
}
