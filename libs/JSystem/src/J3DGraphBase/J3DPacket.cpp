#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/J3DGraphBase/J3DPacket.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphBase/J3DDrawBuffer.h"
#include "JSystem/J3DGraphBase/J3DMaterial.h"
#include "JSystem/J3DGraphBase/J3DShapeMtx.h"
#include "JSystem/J3DGraphBase/J3DMatBlock.h"
#include "JSystem/J3DGraphBase/J3DTexture.h"
#include "JSystem/JKernel/JKRHeap.h"
#include <os.h>
#include <cstring>
#include <cstdlib>
#include "global.h"
#ifdef TARGET_PC
extern int g_pc_verbose;
extern "C" u32 VIGetRetraceCount(void);
#endif

J3DError J3DDisplayListObj::newDisplayList(u32 maxSize) {
    mMaxSize = ALIGN_NEXT(maxSize, 0x20);
    mpDisplayList[0] = new (0x20) char[mMaxSize];
    mpDisplayList[1] = new (0x20) char[mMaxSize];
    mSize = 0;

    if (mpDisplayList[0] == NULL || mpDisplayList[1] == NULL)
        return kJ3DError_Alloc;

    return kJ3DError_Success;
}

J3DError J3DDisplayListObj::newSingleDisplayList(u32 maxSize) {
    mMaxSize = ALIGN_NEXT(maxSize, 0x20);
    mpDisplayList[0] = new (0x20) char[mMaxSize];
    mpDisplayList[1] = mpDisplayList[0];
    mSize = 0;

    if (mpDisplayList[0] == NULL)
        return kJ3DError_Alloc;

    return kJ3DError_Success;
}

int J3DDisplayListObj::single_To_Double() {
    if (mpDisplayList[0] == mpDisplayList[1]) {
        mpDisplayList[1] = new (0x20) char[mMaxSize];

        if (mpDisplayList[1] == NULL)
            return kJ3DError_Alloc;

        memcpy(mpDisplayList[1], mpDisplayList[0], mMaxSize);
        DCStoreRange(mpDisplayList[1], mMaxSize);
    }

    return kJ3DError_Success;
}

void J3DDisplayListObj::setSingleDisplayList(void* pDLData, u32 size) {
    J3D_ASSERT_NULLPTR(148, pDLData != NULL);

    mMaxSize = ALIGN_NEXT(size, 0x20);
    mpDisplayList[0] = pDLData;
    mpDisplayList[1] = mpDisplayList[0];
    mSize = size;
}

void J3DDisplayListObj::swapBuffer() {
    void* pTmp = mpDisplayList[0];
    mpDisplayList[0] = mpDisplayList[1];
    mpDisplayList[1] = pTmp;
}

void J3DDisplayListObj::callDL() const {
    GXCallDisplayList(mpDisplayList[0], mSize);
}

GDLObj J3DDisplayListObj::sGDLObj;

s32 J3DDisplayListObj::sInterruptFlag;

void J3DDisplayListObj::beginDL() {
    swapBuffer();
    sInterruptFlag = OSDisableInterrupts();
    GDInitGDLObj(&sGDLObj, (u8*)mpDisplayList[0], mMaxSize);
    GDSetCurrent(&sGDLObj);
}

u32 J3DDisplayListObj::endDL() {
    GDPadCurr32();
    OSRestoreInterrupts(sInterruptFlag);
    mSize = GDGetGDLObjOffset(&sGDLObj);
    GDFlushCurrToMem();
    GDSetCurrent(NULL);
    return mSize;
}

void J3DDisplayListObj::beginPatch() {
    beginDL();
}

u32 J3DDisplayListObj::endPatch() {
    OSRestoreInterrupts(sInterruptFlag);
    GDSetCurrent(NULL);
    return mSize;
}

int J3DPacket::entry(J3DDrawBuffer* pBuffer) {
    J3D_ASSERT_NULLPTR(290, pBuffer != NULL);
    return 1;
}

void J3DPacket::addChildPacket(J3DPacket* pPacket) {
    J3D_ASSERT_NULLPTR(304, pPacket != NULL);

    if (mpFirstChild == NULL) {
        mpFirstChild = pPacket;
    } else {
        pPacket->setNextPacket(mpFirstChild);
        mpFirstChild = pPacket;
    }
}

static u32 sDifferedRegister[8] = {
    J3DDiffFlag_AmbColor,
    J3DDiffFlag_MatColor,
    J3DDiffFlag_ColorChan,
    J3DDiffFlag_TevReg,
    J3DDiffFlag_Fog,
    J3DDiffFlag_Blend,
    J3DDiffFlag_KonstColor,
    J3DDiffFlag_TevStageIndirect,
};

static s32 sSizeOfDiffered[8] = {
    13,
    13,
    21,
    120,
    55,
    15,
    19,
    45,
};

J3DDrawPacket::J3DDrawPacket() {
    mFlags = 0;
    mpDisplayListObj = NULL;
    mpTexMtxObj = NULL;
}

J3DDrawPacket::~J3DDrawPacket() {}

J3DError J3DDrawPacket::newDisplayList(u32 size) {
    mpDisplayListObj = new J3DDisplayListObj();

    if (mpDisplayListObj == NULL)
        return kJ3DError_Alloc;

    J3DError ret = mpDisplayListObj->newDisplayList(size);
    if (ret != kJ3DError_Success)
        return ret;

    return kJ3DError_Success;
}

J3DError J3DDrawPacket::newSingleDisplayList(u32 size) {
    mpDisplayListObj = new J3DDisplayListObj();

    if (mpDisplayListObj == NULL)
        return kJ3DError_Alloc;

    J3DError ret = mpDisplayListObj->newSingleDisplayList(size);
    if (ret != kJ3DError_Success)
        return ret;

    return kJ3DError_Success;
}

void J3DDrawPacket::draw() {
    callDL();
}

J3DMatPacket::J3DMatPacket() {
    mpInitShapePacket = NULL;
    mpShapePacket = NULL;
    mpMaterial = NULL;
    mDiffFlag = 0xFFFFFFFF;
    mpTexture = NULL;
    mpMaterialAnm = NULL;
}

J3DMatPacket::~J3DMatPacket() {}

void J3DMatPacket::addShapePacket(J3DShapePacket* pShape) {
    if (mpShapePacket == NULL) {
        mpShapePacket = pShape;
    } else {
        pShape->setNextPacket(mpShapePacket);
        mpShapePacket = pShape;
    }
}

void J3DMatPacket::beginDiff() {
    mpInitShapePacket->beginDL();
}

void J3DMatPacket::endDiff() {
    mpInitShapePacket->endDL();
}

bool J3DMatPacket::isSame(J3DMatPacket* pOther) const {
    J3D_ASSERT_NULLPTR(521, pOther != NULL);
    return mDiffFlag == pOther->mDiffFlag && (mDiffFlag >> 31) == 0;
}

void J3DMatPacket::draw() {
#ifdef TARGET_PC
    static int s_matpkt_draw_log = 0;
    static int s_matpkt_late_log = 0;
    u32 frame = VIGetRetraceCount();
    if (g_pc_verbose && (s_matpkt_draw_log < 120 || (frame > 560 && s_matpkt_late_log < 160))) {
        fprintf(stderr,
                "[J3D-MATPKT] frame=%u draw pkt=%p locked=%d mat=%p shared=%p tev=%p idx=%d\n",
                frame,
                (void*)this, isLocked() ? 1 : 0, (void*)mpMaterial,
                mpMaterial ? (void*)mpMaterial->getSharedDisplayListObj() : NULL,
                mpMaterial ? (void*)mpMaterial->getTevBlock() : NULL,
                mpMaterial ? mpMaterial->getIndex() : -1);
        fflush(stderr);
        if (s_matpkt_draw_log < 120) s_matpkt_draw_log++;
        if (frame > 560 && s_matpkt_late_log < 160) s_matpkt_late_log++;
    }
#endif
#ifdef TARGET_PC
    const char* skipMatDlEnv = std::getenv("TP_SKIP_MAT_DL");
    const bool skipMatDl = (skipMatDlEnv != NULL) && (std::atoi(skipMatDlEnv) != 0);
    const char* useSharedMatDlEnv = std::getenv("TP_USE_SHARED_MAT_DL");
    const bool useSharedMatDl = (useSharedMatDlEnv == NULL) || (std::atoi(useSharedMatDlEnv) != 0);
    if (!skipMatDl) {
        if (useSharedMatDl && mpMaterial->getSharedDisplayListObj() != NULL) {
            mpMaterial->loadSharedDL();
        } else {
            mpMaterial->load();
        }
        if (getDisplayListObj() != NULL) {
            callDL();
        }
        /* On PC, the shared DL contains GC physical addresses for textures
         * which don't work. Explicitly load textures via GXLoadTexObj. */
        {
            /* Prefer packet-local texture table; j3dSys texture can be stale
             * when multiple models draw in the same frame. */
            J3DTexture* tex = (mpTexture != NULL) ? mpTexture : j3dSys.getTexture();
            if (tex != NULL) {
                j3dSys.setTexture(tex);
            }
            J3DTevBlock* tevBlock = mpMaterial->getTevBlock();
            static int s_texload_log = 0;
            if (g_pc_verbose && s_texload_log < 10) {
                fprintf(stderr, "[J3D-TEX] mat=%p tex=%p tevBlock=%p\n",
                        (void*)mpMaterial, (void*)tex, (void*)tevBlock);
                if (tevBlock) {
                    fprintf(stderr, "[J3D-TEX]   stages=%d texNo[0..3]=%d,%d,%d,%d\n",
                            tevBlock->getTevStageNum(),
                            tevBlock->getTexNo(0), tevBlock->getTexNo(1),
                            tevBlock->getTexNo(2), tevBlock->getTexNo(3));
                }
                if (tex) {
                    fprintf(stderr, "[J3D-TEX]   numTex=%d\n", tex->getNum());
                }
                s_texload_log++;
                fflush(stderr);
            }
            if (tex != NULL && tevBlock != NULL) {
                int numStages = tevBlock->getTevStageNum();
                if (numStages > 8) numStages = 8;
                bool anyTexLoaded = false;
                for (int i = 0; i < numStages; i++) {
                    u16 texNoRaw = tevBlock->getTexNo(i);
                    u16 texNo = texNoRaw;
                    if (texNo == 0xFFFF && tex->getNum() > 0) {
                        texNo = (i < (int)tex->getNum()) ? (u16)i : 0;
                        if (g_pc_verbose) {
                            static int s_texno_missing_log = 0;
                            if (s_texno_missing_log++ < 40) {
                                fprintf(stderr,
                                        "[J3D-TEX] substitute missing texNo stage=%d -> %u numTex=%u\n",
                                        i, texNo, tex->getNum());
                            }
                        }
                    }
                    if (texNo != 0xFFFF && texNo >= tex->getNum()) {
                        u16 swapped = (u16)((texNoRaw >> 8) | (texNoRaw << 8));
                        if (swapped < tex->getNum()) {
                            texNo = swapped;
                            if (g_pc_verbose) {
                                static int s_texno_fix_log = 0;
                                if (s_texno_fix_log++ < 40) {
                                    fprintf(stderr,
                                            "[J3D-TEX] fix texNo stage=%d raw=%u swapped=%u numTex=%u\n",
                                            i, texNoRaw, texNo, tex->getNum());
                                }
                            }
                        }
                    }
                    if (texNo != 0xFFFF && texNo < tex->getNum()) {
                        tex->loadGX(texNo, (GXTexMapID)i);
                        anyTexLoaded = true;
                    }
                }
                /* Fallback: if no textures were loaded from TevBlock (byte-swap
                 * corruption makes all texNo = 0xFFFF), load textures and fix
                 * the TEV stage state so they actually get sampled. */
                if (!anyTexLoaded && tex->getNum() > 0) {
                    static int s_fallback_log = 0;
                    if (s_fallback_log++ < 5) {
                        fprintf(stderr, "[J3D-TEX] FALLBACK: loading %d textures (model has %d)\n",
                                numStages < (int)tex->getNum() ? numStages : (int)tex->getNum(),
                                (int)tex->getNum());
                        fflush(stderr);
                    }
                    /* Just load texture 0 to map 0 — the simple shader checks
                     * g_gx.gl_textures[0] directly regardless of TEV_ORDER enable. */
                    tex->loadGX(0, (GXTexMapID)0);
                }
            }
        }
    } else {
        /* Keep startup scene rendering alive while MAT3/TEV reconstruction is in progress. */
        GXSetNumChans(1);
        GXSetNumTexGens(1);
        GXSetNumTevStages(1);
    }
#else
    if (mpMaterial->getSharedDisplayListObj() != NULL) {
        mpMaterial->loadSharedDL();
    } else {
        mpMaterial->load();
    }
    if (getDisplayListObj() != NULL) {
        callDL();
    }
#endif

    J3DShapePacket* packet = getShapePacket();
#ifdef TARGET_PC
    if (packet == NULL || packet->getShape() == NULL) {
        static int s_noshape = 0;
        if (s_noshape++ < 5) fprintf(stderr, "[J3D-MATPKT] no shape packet or shape!\n");
        return;
    }
#endif
    packet->getShape()->loadPreDrawSetting();

    int shapeCount = 0;
    while (packet != NULL) {
        if (packet->getDisplayListObj() != NULL) {
            packet->getDisplayListObj()->callDL();
        }

        packet->drawFast();
        shapeCount++;
        packet = (J3DShapePacket*)packet->getNextPacket();
    }
#ifdef TARGET_PC
    {
        static int s_matdraw = 0;
        if (s_matdraw < 10) {
            fprintf(stderr, "[J3D-MATPKT] drew %d shapes\n", shapeCount);
            s_matdraw++;
        }
    }
#endif

    J3DShape::resetVcdVatCache();
}

J3DShapePacket::J3DShapePacket() {
    mpShape = NULL;
    mpMtxBuffer = NULL;
    mpBaseMtxPtr = NULL;
    mDiffFlag = 0;
    mpModel = NULL;
}

J3DShapePacket::~J3DShapePacket() {}

u32 J3DShapePacket::calcDifferedBufferSize(u32 diffFlags) {
    u32 bufferSize = 0;

    for (u32 i = 0; i < 8; i++) {
        if ((diffFlags & sDifferedRegister[i]) != 0) {
            bufferSize += sSizeOfDiffered[i];
        }
    }

    u32 lightObjNum = getDiffFlag_LightObjNum(diffFlags);
    bufferSize += lightObjNum * 0x48;

    u32 texGenNum = getDiffFlag_TexGenNum(diffFlags);
    if (texGenNum != 0) {
        u32 mat_texGenNum = mpShape->getMaterial()->getTexGenNum();
        u32 sp30 = texGenNum > mat_texGenNum ? texGenNum : mat_texGenNum;

        if (diffFlags & J3DDiffFlag_TexGen) {
            bufferSize += calcDifferedBufferSize_TexGenSize(sp30);
        } else {
            bufferSize += calcDifferedBufferSize_TexMtxSize(sp30);
        }
    }

    u32 texNoNum = getDiffFlag_TexNoNum(diffFlags);
    if (texNoNum != 0) {
        u8 sp9;
        if (mpShape->getMaterial()->getTevStageNum() > 8) {
            sp9 = 8;
        } else {
            sp9 = mpShape->getMaterial()->getTevStageNum();
        }

        u32 mat_texNoNum = sp9;
        u32 sp24 = texNoNum > mat_texNoNum ? texNoNum : mat_texNoNum;

        if (diffFlags & J3DDiffFlag_TexCoordScale) {
            bufferSize += calcDifferedBufferSize_TexNoAndTexCoordScaleSize(sp24);
        } else {
            bufferSize += calcDifferedBufferSize_TexNoSize(sp24);
        }
    }

    u32 tevStageNum = getDiffFlag_TevStageNum(diffFlags);
    if (tevStageNum != 0) {
        u8 sp8;
        if (mpShape->getMaterial()->getTevStageNum() > 8) {
            sp8 = 8;
        } else {
            sp8 = mpShape->getMaterial()->getTevStageNum();
        }

        u32 mat_tevStageNum = sp8;
        u32 sp18 = tevStageNum > mat_tevStageNum ? tevStageNum : mat_tevStageNum;

        bufferSize += calcDifferedBufferSize_TevStageSize(sp18);
        if (diffFlags & J3DDiffFlag_TevStageIndirect) {
            bufferSize += calcDifferedBufferSize_TevStageDirectSize(sp18);
        }
    }

    return OSRoundUp32B(bufferSize);
}

int J3DShapePacket::newDifferedDisplayList(u32 diffFlags) {
    mDiffFlag = diffFlags;

    u32 bufSize = calcDifferedBufferSize(diffFlags);
    int ret = newDisplayList(bufSize);
    if (ret != kJ3DError_Success) {
        return ret;
    }

    J3DDisplayListObj* dlobj = getDisplayListObj();
    setDisplayListObj(dlobj);
    return kJ3DError_Success;
}

void J3DShapePacket::prepareDraw() const {
    mpModel->getVertexBuffer()->setArray();
    j3dSys.setModel(mpModel);
    j3dSys.setShapePacket((J3DShapePacket*)this);

    J3DShapeMtx::setLODFlag(mpModel->checkFlag(J3DMdlFlag_EnableLOD) != 0);

    if (mpModel->checkFlag(J3DMdlFlag_SkinPosCpu)) {
        mpShape->onFlag(J3DShpFlag_SkinPosCpu);
    } else {
        mpShape->offFlag(J3DShpFlag_SkinPosCpu);
    }

    if (mpModel->checkFlag(J3DMdlFlag_SkinNrmCpu) &&
        mpShape->checkFlag(J3DShpFlag_EnableLod) == false)
    {
        mpShape->onFlag(J3DShpFlag_SkinNrmCpu);
    } else {
        mpShape->offFlag(J3DShpFlag_SkinNrmCpu);
    }

    mpShape->setCurrentViewNoPtr(mpMtxBuffer->getCurrentViewNoPtr());
    mpShape->setScaleFlagArray(mpMtxBuffer->getScaleFlagArray());
    mpShape->setDrawMtx(mpMtxBuffer->getDrawMtxPtrPtr());

    if (!mpShape->getNBTFlag()) {
        mpShape->setNrmMtx(mpMtxBuffer->getNrmMtxPtrPtr());
    } else {
        mpShape->setNrmMtx(mpMtxBuffer->mpBumpMtxArr[1][mpShape->getBumpMtxOffset()]);
    }

    mpModel->getModelData()->syncJ3DSysFlags();
}

void J3DShapePacket::draw() {
#ifdef TARGET_PC
    static int s_shp_draw = 0;
    if (s_shp_draw < 10) {
        fprintf(stderr, "[J3D-SHAPE] draw: hidden=%d shape=%p dlObj=%p flags=0x%x\n",
                checkFlag(J3DShpFlag_Hidden), (void*)mpShape, (void*)mpDisplayListObj, mFlags);
        s_shp_draw++;
    }
#endif
    if (!checkFlag(J3DShpFlag_Hidden) && mpShape != NULL) {
        prepareDraw();

        if (mpTexMtxObj != NULL) {
            J3DDifferedTexMtx::sTexGenBlock = mpShape->getMaterial()->getTexGenBlock();
            J3DDifferedTexMtx::sTexMtxObj = getTexMtxObj();
        } else {
            J3DDifferedTexMtx::sTexGenBlock = NULL;
        }

        if (mpDisplayListObj != NULL) {
            mpDisplayListObj->callDL();
        }

#ifdef TARGET_PC
        if (s_shp_draw <= 10) {
            fprintf(stderr, "[J3D-SHAPE] calling shape->draw() shape=%p\n", (void*)mpShape);
        }
#endif
        mpShape->draw();
    }
}

void J3DShapePacket::drawFast() {
    if (!checkFlag(J3DShpFlag_Hidden) && mpShape != NULL) {
        prepareDraw();

        if (mpTexMtxObj != NULL) {
            J3DDifferedTexMtx::sTexGenBlock = mpShape->getMaterial()->getTexGenBlock();
            J3DDifferedTexMtx::sTexMtxObj = getTexMtxObj();
        } else {
            J3DDifferedTexMtx::sTexGenBlock = NULL;
        }

        mpShape->drawFast();
    }
}

void J3DPacket::draw() {}

int J3DMatPacket::entry(J3DDrawBuffer* pBuffer) {
    J3DDrawBuffer::sortFunc func = J3DDrawBuffer::sortFuncTable[pBuffer->getSortMode()];
    return (pBuffer->*func)(this);
}
