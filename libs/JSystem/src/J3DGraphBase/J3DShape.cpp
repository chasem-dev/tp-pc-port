#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/J3DGraphBase/J3DShape.h"
#include "JSystem/J3DGraphBase/J3DPacket.h"
#include "JSystem/J3DGraphBase/J3DVertex.h"
#include "JSystem/J3DGraphBase/J3DFifo.h"
#include <gd.h>
#ifdef TARGET_PC
extern int g_pc_verbose;
extern "C" void pc_gx_set_array_count(u32 attr, u32 count);
#endif

void J3DGDSetVtxAttrFmtv(_GXVtxFmt, GXVtxAttrFmtList const*, bool);
void J3DFifoLoadPosMtxImm(Mtx, u32);
void J3DFifoLoadNrmMtxImm(Mtx, u32);

static const u32 kMaxSafeVtxDescEntries = 32;
static const u32 kMaxSafeVtxAttrFmtEntries = 32;

void J3DShape::initialize() {
    mMaterial = NULL;
    mIndex = -1;
    mMtxGroupNum = 0;
    mFlags = 0;
    mRadius = 0.0f;
    mMin.x = 0.0f;
    mMin.y = 0.0f;
    mMin.z = 0.0f;
    mMax.x = 0.0f;
    mMax.y = 0.0f;
    mMax.z = 0.0f;
    mVtxDesc = NULL;
    mShapeMtx = NULL;
    mShapeDraw = NULL;
    mVertexData = NULL;
    mDrawMtxData = NULL;
    mScaleFlagArray = NULL;
    mDrawMtx = NULL;
    mNrmMtx = NULL;
    mCurrentViewNo = &j3dDefaultViewNo;
    mHasNBT = false;
    mHasPNMTXIdx = false;
}

void J3DShape::addTexMtxIndexInDL(GXAttr attr, u32 valueBase) {
    u32 kSize[] = {0, 1, 1, 2};

    s32 pnmtxidxOffs = -1;
    s32 attrOffs = -1;
    s32 stride = 0;
    bool found = false;

    u32 safeDescCount = 0;
    for (GXVtxDescList* vtxDesc = getVtxDesc();
         vtxDesc->attr != GX_VA_NULL && safeDescCount++ < kMaxSafeVtxDescEntries; vtxDesc++)
    {
        if (vtxDesc->attr == GX_VA_PNMTXIDX)
            pnmtxidxOffs = stride;

        if (attr < vtxDesc->attr && !found) {
            attrOffs = stride;
            found = true;
        }

        if ((u32)vtxDesc->type >= 4) {
            continue;
        }
        stride = stride + kSize[vtxDesc->type];
    }

    if (pnmtxidxOffs == -1)
        return;

    for (u16 i = 0; i < (u16)getMtxGroupNum(); i++)
        getShapeDraw(i)->addTexMtxIndexInDL(stride, attrOffs, (s32)valueBase);
}

void J3DShape::addTexMtxIndexInVcd(GXAttr attr) {
    u32 kSize[] = {0, 1, 1, 2};  // stripped data

    s32 attrIdx = -1;
    s32 attrOffs = -1;
    s32 stride = 0;

    GXVtxDescList* vtxDesc = getVtxDesc();
    s32 attrCount = 0;

    u32 safeDescCount = 0;
    for (; vtxDesc->attr != GX_VA_NULL && safeDescCount++ < kMaxSafeVtxDescEntries; vtxDesc++) {
        if (vtxDesc->attr == GX_VA_PNMTXIDX) {
            attrIdx = stride;
        }
        attrCount++;
    }

    if (attrIdx == -1)
        return;

    GXVtxDescList* newVtxDesc = new GXVtxDescList[attrCount + 2];
    bool inserted = false;

    vtxDesc = getVtxDesc();
    GXVtxDescList* dst = newVtxDesc;
    safeDescCount = 0;
    for (; vtxDesc->attr != GX_VA_NULL && safeDescCount++ < kMaxSafeVtxDescEntries; vtxDesc++) {
        if ((attr < vtxDesc->attr) && !inserted) {
            dst->attr = attr;
            dst->type = GX_DIRECT;
            attrOffs = stride;
            dst++;

            inserted = true;
        }

        dst->attr = vtxDesc->attr;
        dst->type = vtxDesc->type;
        if ((u32)vtxDesc->type >= 4) {
            dst++;
            continue;
        }
        stride = stride + kSize[vtxDesc->type];
        dst++;
    }

    dst->attr = GX_VA_NULL;
    dst->type = GX_NONE;
    mVtxDesc = newVtxDesc;
    makeVcdVatCmd();
}

void J3DShape::calcNBTScale(const Vec& param_0, f32 (*param_1)[3][3], f32 (*param_2)[3][3]) {
    for (u16 i = 0; i < mMtxGroupNum; i++)
        mShapeMtx[i]->calcNBTScale(param_0, param_1, param_2);
}

u16 J3DShape::countBumpMtxNum() const {
    u16 num = 0;
    for (u16 i = 0; i < mMtxGroupNum; i++)
        num += mShapeMtx[i]->getUseMtxNum();

    return num;
}

void J3DLoadCPCmd(u8 addr, u32 val) {
    GXCmd1u8(GX_LOAD_CP_REG);
    GXCmd1u8(addr);
    GXCmd1u32(val);
}

static void J3DLoadArrayBasePtr(GXAttr attr, void* data) {
#ifdef TARGET_PC
    /* On PC, store the full 64-bit pointer via GXSetArray.
     * GC used 26-bit physical addresses via CP registers which can't hold 64-bit pointers. */
    if (data != NULL)
        GXSetArray(attr, data, 0);
#else
    u32 idx = (attr == GX_VA_NBT) ? 1 : (attr - GX_VA_POS);
    J3DLoadCPCmd(0xA0 + idx, ((uintptr_t)data & 0x7FFFFFFF));
#endif
}

void J3DShape::loadVtxArray() const {
    J3DLoadArrayBasePtr(GX_VA_POS, j3dSys.getVtxPos());

    if (!mHasNBT) {
        J3DLoadArrayBasePtr(GX_VA_NRM, j3dSys.getVtxNrm());
    }

    J3DLoadArrayBasePtr(GX_VA_CLR0, j3dSys.getVtxCol());
}

bool J3DShape::isSameVcdVatCmd(J3DShape* other) {
    u8* a = (u8*)other->getVcdVatCmd();
    u8* b = mVcdVatCmd;
    for (u32 i = 0; i < kVcdVatDLSize; i++)
        if (a[i] != b[i])
            return false;

    return true;
}

void J3DShape::makeVtxArrayCmd() {
    GXVtxAttrFmtList* vtxAttr = mVertexData->getVtxAttrFmtList();

    u8 stride[12];
    void* array[12];
    u32 arrayCount[12];
    for (u32 i = 0; i < 12; i++) {
        stride[i] = 0;
        array[i] = 0;
        arrayCount[i] = 0;
    }

    u32 safeFmtCount = 0;
    for (; vtxAttr->attr != GX_VA_NULL && safeFmtCount++ < kMaxSafeVtxAttrFmtEntries; vtxAttr++) {
        switch (vtxAttr->attr) {
        case GX_VA_POS: {
            if (vtxAttr->type == GX_F32)
                stride[vtxAttr->attr - GX_VA_POS] = 12;
            else
                stride[vtxAttr->attr - GX_VA_POS] = 6;

            array[vtxAttr->attr - GX_VA_POS] = mVertexData->getVtxPosArray();
            arrayCount[vtxAttr->attr - GX_VA_POS] = mVertexData->getVtxNum();
            u8 posFrac = (u8)vtxAttr->frac;
            if (posFrac > 16) {
                posFrac = 0;
            }
            mVertexData->setVtxPosFrac(posFrac);
            mVertexData->setVtxPosType((GXCompType)vtxAttr->type);
        } break;
        case GX_VA_NRM: {
            if (vtxAttr->type == GX_F32)
                stride[vtxAttr->attr - GX_VA_POS] = 12;
            else
                stride[vtxAttr->attr - GX_VA_POS] = 6;

            array[vtxAttr->attr - GX_VA_POS] = mVertexData->getVtxNrmArray();
            arrayCount[vtxAttr->attr - GX_VA_POS] = mVertexData->getNrmNum();
            u8 nrmFrac = (u8)vtxAttr->frac;
            if (nrmFrac > 16) {
                nrmFrac = 0;
            }
            mVertexData->setVtxNrmFrac(nrmFrac);
            mVertexData->setVtxNrmType((GXCompType)vtxAttr->type);
        } break;
        case GX_VA_CLR0:
        case GX_VA_CLR1: {
            stride[vtxAttr->attr - GX_VA_POS] = 4;
            array[vtxAttr->attr - GX_VA_POS] = mVertexData->getVtxColorArray(vtxAttr->attr - GX_VA_CLR0);
            arrayCount[vtxAttr->attr - GX_VA_POS] = mVertexData->getColNum();
        } break;
        case GX_VA_TEX0:
        case GX_VA_TEX1:
        case GX_VA_TEX2:
        case GX_VA_TEX3:
        case GX_VA_TEX4:
        case GX_VA_TEX5:
        case GX_VA_TEX6:
        case GX_VA_TEX7: {
            if (vtxAttr->type == GX_F32)
                stride[vtxAttr->attr - GX_VA_POS] = 8;
            else
                stride[vtxAttr->attr - GX_VA_POS] = 4;

            array[vtxAttr->attr - GX_VA_POS] = mVertexData->getVtxTexCoordArray(vtxAttr->attr - GX_VA_TEX0);
            arrayCount[vtxAttr->attr - GX_VA_POS] = mVertexData->getTexCoordNum();
        } break;
        default:
            break;
        }
    }

    GXVtxDescList* vtxDesc = mVtxDesc;
    mHasPNMTXIdx = false;
    u32 safeDescCount = 0;
    for (; vtxDesc->attr != GX_VA_NULL && safeDescCount++ < kMaxSafeVtxDescEntries; vtxDesc++) {
        if (vtxDesc->attr == GX_VA_NBT && vtxDesc->type != GX_NONE) {
            mHasNBT = true;
            stride[GX_VA_NRM - GX_VA_POS] *= 3;
            array[GX_VA_NRM - GX_VA_POS] = mVertexData->getVtxNBTArray();
            arrayCount[GX_VA_NRM - GX_VA_POS] = mVertexData->getNrmNum();
        } else if (vtxDesc->attr == GX_VA_PNMTXIDX && vtxDesc->type != GX_NONE) {
            mHasPNMTXIdx = true;
        }
    }

    for (u32 i = 0; i < 12; i++) {
#ifdef TARGET_PC
        pc_gx_set_array_count((u32)(i + GX_VA_POS), arrayCount[i]);
#endif
        if (array[i] != 0)
            GDSetArray((GXAttr)(i + GX_VA_POS), array[i], stride[i]);
        else
            GDSetArrayRaw((GXAttr)(i + GX_VA_POS), 0, stride[i]);
    }
}

void J3DShape::makeVcdVatCmd() {
    static BOOL sInterruptFlag = OSDisableInterrupts();
    OSDisableScheduler();

    GDLObj gdl_obj;
    GDInitGDLObj(&gdl_obj, mVcdVatCmd, kVcdVatDLSize);
    GDSetCurrent(&gdl_obj);
    GDSetVtxDescv(mVtxDesc);
    makeVtxArrayCmd();
    J3DGDSetVtxAttrFmtv(GX_VTXFMT0, mVertexData->getVtxAttrFmtList(), mHasNBT);
    GDPadCurr32();
    GDFlushCurrToMem();
    GDSetCurrent(NULL);
    OSEnableScheduler();
    OSRestoreInterrupts(sInterruptFlag);
}

void* J3DShape::sOldVcdVatCmd;

void J3DShape::loadCurrentMtx() const {
    mCurrentMtx.load();
}

void J3DShape::loadPreDrawSetting() const {
#ifdef TARGET_PC
    /* On PC, makeVcdVatCmd is skipped so all mVcdVatCmd buffers are zeroed.
     * sortVcdVatCmd then shares the same pointer across all shapes, making
     * the sOldVcdVatCmd != mVcdVatCmd check useless — it would only set VCD
     * for the first shape, and all others would inherit the wrong VCD.
     * Use mVtxDesc pointer as the comparison key instead: each shape with
     * a different vertex format has a different mVtxDesc. */
    static const GXVtxDescList* sOldVtxDesc = NULL;
    {   /* Always update VCD on PC — force per-shape recalculation */
        static int s_vtxdesc_dump = 0;
        if (g_pc_verbose && s_vtxdesc_dump < 30) {
            fprintf(stderr, "[VCD-DUMP] shape=%d vtxDescPtr=%p entries:", mIndex, (void*)mVtxDesc);
            u32 cnt = 0;
            for (const GXVtxDescList* d = mVtxDesc;
                 d->attr != GX_VA_NULL && cnt++ < 16; d++)
                fprintf(stderr, " %u=%u", (unsigned)d->attr, (unsigned)d->type);
            fprintf(stderr, "\n");
            fflush(stderr);
            s_vtxdesc_dump++;
        }
        GXClearVtxDesc();
        bool badDesc = false;
        u32 validDescCount = 0;
        bool hasPos = false;
        bool hasNrm = false;
        bool hasTex0 = false;
        u32 safeDescCount = 0;
        for (const GXVtxDescList* desc = mVtxDesc;
             desc->attr != GX_VA_NULL && safeDescCount++ < kMaxSafeVtxDescEntries; desc++)
        {
            if ((u32)desc->attr > GX_VA_TEX7 || (u32)desc->type > GX_INDEX16) {
                badDesc = true;
                break;
            }
            GXSetVtxDesc(desc->attr, desc->type);
            validDescCount++;
            hasPos |= (desc->attr == GX_VA_POS);
            hasNrm |= (desc->attr == GX_VA_NRM || desc->attr == GX_VA_NBT);
            hasTex0 |= (desc->attr == GX_VA_TEX0);
        }

        if (badDesc || validDescCount == 0 || !hasPos) {
            GXClearVtxDesc();
            GXSetVtxDesc(GX_VA_POS, GX_INDEX16);
            if (!hasNrm) {
                GXSetVtxDesc(GX_VA_NRM, GX_INDEX16);
            }
            if (!hasTex0) {
                GXSetVtxDesc(GX_VA_TEX0, GX_INDEX16);
            }
        }

        if (mVertexData) {
            GXVtxAttrFmtList* fmts = mVertexData->getVtxAttrFmtList();
            u32 safeFmtCount = 0;
            for (; fmts->attr != GX_VA_NULL && safeFmtCount++ < kMaxSafeVtxAttrFmtEntries; fmts++) {
                u8 frac = fmts->frac;
                if (frac > 16) {
                    frac = 0;
                }
                GXSetVtxAttrFmt(GX_VTXFMT0, fmts->attr, fmts->cnt, fmts->type, frac);
            }
        }
        sOldVtxDesc = mVtxDesc;
        sOldVcdVatCmd = mVcdVatCmd;
    }
    mCurrentMtx.load();
#else
    if (sOldVcdVatCmd != mVcdVatCmd) {
        GXCallDisplayList(mVcdVatCmd, kVcdVatDLSize);
        sOldVcdVatCmd = mVcdVatCmd;
    }
    mCurrentMtx.load();
#endif
}

bool J3DShape::sEnvelopeFlag;

void J3DShape::setArrayAndBindPipeline() const {
    J3DShapeMtx::setCurrentPipeline((mFlags & 0x1C) >> 2);
    loadVtxArray();
    j3dSys.setModelDrawMtx(mDrawMtx[*mCurrentViewNo]);
    j3dSys.setModelNrmMtx(mNrmMtx[*mCurrentViewNo]);
    J3DShapeMtx::sCurrentScaleFlag = mScaleFlagArray;
    J3DShapeMtx::sNBTFlag = mHasNBT;
    sEnvelopeFlag = mHasPNMTXIdx;
    J3DShapeMtx::sTexMtxLoadType = getTexMtxLoadType();
}

void J3DShape::drawFast() const {
    if (sOldVcdVatCmd != mVcdVatCmd) {
        GXCallDisplayList(mVcdVatCmd, kVcdVatDLSize);
        sOldVcdVatCmd = mVcdVatCmd;
    }

    if (sEnvelopeFlag != 0 && !mHasPNMTXIdx)
        mCurrentMtx.load();

    setArrayAndBindPipeline();
    if (!checkFlag(J3DShpFlag_NoMtx)) {
        if (J3DShapeMtx::getLODFlag())
            J3DShapeMtx::resetMtxLoadCache();

        for (u16 n = mMtxGroupNum, i = 0; i < n; i++) {
            if (mShapeMtx[i] != NULL)
                mShapeMtx[i]->load();
            if (mShapeDraw[i] != NULL)
                mShapeDraw[i]->draw();
        }
    } else {
        J3DFifoLoadPosMtxImm(*j3dSys.getShapePacket()->getBaseMtxPtr(), GX_PNMTX0);
        J3DFifoLoadNrmMtxImm(*j3dSys.getShapePacket()->getBaseMtxPtr(), GX_PNMTX0);
        for (u16 n = mMtxGroupNum, i = 0; i < n; i++)
            if (mShapeDraw[i] != NULL)
                mShapeDraw[i]->draw();
    }
}

void J3DShape::draw() const {
    resetVcdVatCache();
    loadPreDrawSetting();
    drawFast();
}

void J3DShape::simpleDraw() const {
    resetVcdVatCache();
    loadPreDrawSetting();
    J3DShapeMtx::setCurrentPipeline((mFlags & 0x1C) >> 2);
    loadVtxArray();
#ifdef TARGET_PC
    static int s_sd = 0;
    static bool s_logged_nonzero = false;
    s_sd++;
    if (!s_logged_nonzero && mMtxGroupNum > 0) {
        s_logged_nonzero = true;
        fprintf(stderr, "[J3D] simpleDraw: shape=%p idx=%d mtxGrps=%d (call #%d)\n",
                (void*)this, mIndex, mMtxGroupNum, s_sd);
        for (u16 i = 0; i < mMtxGroupNum && i < 4; i++) {
            J3DShapeDraw* sd = mShapeDraw[i];
            fprintf(stderr, "  grp[%d]: draw=%p dl=%p dlSize=%d\n", i, (void*)sd,
                    sd ? (void*)sd->getDisplayList() : NULL,
                    sd ? sd->getDisplayListSize() : 0);
        }
        fflush(stderr);
    }
    if (s_sd <= 3) {
        fprintf(stderr, "[J3D] simpleDraw #%d: mtxGrps=%d\n", s_sd, mMtxGroupNum);
        fflush(stderr);
    }
#endif
    for (u16 n = mMtxGroupNum, i = 0; i < n; i++) {
        if (mShapeDraw[i] != NULL) {
            mShapeDraw[i]->draw();
        }
    }
}

void J3DShape::simpleDrawCache() const {
    if (sOldVcdVatCmd != mVcdVatCmd) {
        GXCallDisplayList(mVcdVatCmd, kVcdVatDLSize);
        sOldVcdVatCmd = mVcdVatCmd;
    }

    if (sEnvelopeFlag && !mHasPNMTXIdx)
        mCurrentMtx.load();

    loadVtxArray();
    for (u16 n = mMtxGroupNum, i = 0; i < n; i++)
        if (mShapeDraw[i] != NULL)
            mShapeDraw[i]->draw();
}
