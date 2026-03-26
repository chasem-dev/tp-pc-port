#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/J3DGraphBase/J3DMaterial.h"
#include "JSystem/J3DGraphBase/J3DGD.h"
#ifdef TARGET_PC
#include <cstdio>
#include <setjmp.h>
extern "C" void pc_gd_clear_overflow_flag(void);
extern "C" int pc_gd_consume_overflow_flag(void);
extern "C" void pc_crash_set_jmpbuf(jmp_buf* buf);
extern "C" jmp_buf* pc_crash_get_jmpbuf(void);
extern "C" uintptr_t pc_crash_get_addr(void);
#endif

J3DColorBlock* J3DMaterial::createColorBlock(u32 flags) {
    J3DColorBlock* rv = NULL;
    switch (flags) {
    case 0:
        rv = new J3DColorBlockLightOff();
        break;
    case 0x40000000:
        rv = new J3DColorBlockLightOn();
        break;
    case 0x80000000:
        rv = new J3DColorBlockAmbientOn();
        break;
    }

    J3D_ASSERT_ALLOCMEM(55, rv != NULL);
    return rv;
}

J3DTexGenBlock* J3DMaterial::createTexGenBlock(u32 flags) {
    J3DTexGenBlock* rv = NULL;
    switch (flags) {
    case 0x8000000:
        rv = new J3DTexGenBlock4();
        break;
    case 0:
    default:
        rv = new J3DTexGenBlockBasic();
    }

    J3D_ASSERT_ALLOCMEM(83, rv != NULL);
    return rv;
}

J3DTevBlock* J3DMaterial::createTevBlock(int tevStageNum) {
    J3DTevBlock* rv = NULL;
    if (tevStageNum <= 1) {
        rv = new J3DTevBlock1();
    } else if (tevStageNum == 2) {
        rv = new J3DTevBlock2();
    } else if (tevStageNum <= 4) {
        rv = new J3DTevBlock4();
    } else if (tevStageNum <= 16) {
        rv = new J3DTevBlock16();
    }

#ifdef TARGET_PC
    if (rv == NULL) {
        fprintf(stderr,
                "[J3D] createTevBlock: invalid tevStageNum=%d, falling back to 1-stage block\n",
                tevStageNum);
        rv = new J3DTevBlock1();
    }
#endif

    J3D_ASSERT_ALLOCMEM(116, rv != NULL);
    return rv;
}

J3DIndBlock* J3DMaterial::createIndBlock(int flags) {
    J3DIndBlock* rv = NULL;
    if (flags != 0) {
        rv = new J3DIndBlockFull();
    } else {
        rv = new J3DIndBlockNull();
    }

    J3D_ASSERT_ALLOCMEM(139, rv != NULL);
    return rv;
}

J3DPEBlock* J3DMaterial::createPEBlock(u32 flags, u32 materialMode) {
    J3DPEBlock* rv = NULL;
    if (flags == 0) {
        if (materialMode & 1) {
            rv = new J3DPEBlockOpa();
            J3D_ASSERT_ALLOCMEM(166, rv != NULL);
            return rv;
        } else if (materialMode & 2) {
            rv = new J3DPEBlockTexEdge();
            J3D_ASSERT_ALLOCMEM(172, rv != NULL);
            return rv;
        } else if (materialMode & 4) {
            rv = new J3DPEBlockXlu();
            J3D_ASSERT_ALLOCMEM(178, rv != NULL);
            return rv;
        }
    }

    if (flags == 0x10000000) {
        rv = new J3DPEBlockFull();
    } else if (flags == 0x20000000) {
        rv = new J3DPEBlockFogOff();
    }

#ifdef TARGET_PC
    if (rv == NULL) {
        fprintf(stderr,
                "[J3D] createPEBlock: invalid flags=0x%08x mode=0x%08x, falling back to Opa block\n",
                flags, materialMode);
        rv = new J3DPEBlockOpa();
    }
#endif

    J3D_ASSERT_ALLOCMEM(188, rv != NULL);
    return rv;
}

u32 J3DMaterial::calcSizeColorBlock(u32 flags) {
    u32 rv = 0;
    switch (flags) {
    case 0:
        rv += sizeof(J3DColorBlockLightOff);
        break;
    case 0x40000000:
        rv += sizeof(J3DColorBlockLightOn);
        break;
    case 0x80000000:
        rv += sizeof(J3DColorBlockAmbientOn);
        break;
    }

    return rv;
}

u32 J3DMaterial::calcSizeTexGenBlock(u32 flags) {
    u32 rv = 0;
    switch (flags) {
    case 0x8000000:
        rv += sizeof(J3DTexGenBlock4);
        break;
    case 0:
    default:
        rv += sizeof(J3DTexGenBlockBasic);
    }

    return rv;
}

u32 J3DMaterial::calcSizeTevBlock(int tevStageNum) {
    u32 rv = 0;
    if (tevStageNum <= 1) {
        rv += sizeof(J3DTevBlock1);
    } else if (tevStageNum == 2) {
        rv += sizeof(J3DTevBlock2);
    } else if (tevStageNum <= 4) {
        rv += sizeof(J3DTevBlock4);
    } else if (tevStageNum <= 16) {
        rv += sizeof(J3DTevBlock16);
    }

    return rv;
}

u32 J3DMaterial::calcSizeIndBlock(int flags) {
    u32 rv = 0;
    if (flags != 0) {
        rv += sizeof(J3DIndBlockFull);
    } else {
        rv += sizeof(J3DIndBlockNull);
    }

    return rv;
}

u32 J3DMaterial::calcSizePEBlock(u32 flags, u32 materialMode) {
    u32 rv = 0;
    if (flags == 0) {
        if (materialMode & 1) {
            rv += sizeof(J3DPEBlockOpa);
        } else if (materialMode & 2) {
            rv += sizeof(J3DPEBlockTexEdge);
        } else if (materialMode & 4) {
            rv += sizeof(J3DPEBlockXlu);
        }
    } else if (flags == 0x10000000) {
        rv += sizeof(J3DPEBlockFull);
    } else if (flags == 0x20000000) {
        rv += sizeof(J3DPEBlockFogOff);
    }

    return rv;
}

void J3DMaterial::initialize() {
    mShape = NULL;
    mNext = NULL;
    mJoint = NULL;
    mMaterialMode = 1;
    mIndex = -1;
    mInvalid = 0;
    mDiffFlag = 0;
    mColorBlock = NULL;
    mTexGenBlock = NULL;
    mTevBlock = NULL;
    mIndBlock = NULL;
    mPEBlock = NULL;
    mpOrigMaterial = NULL;
    mMaterialAnm = NULL;
    mSharedDLObj = NULL;
}

u32 J3DMaterial::countDLSize() {
#ifdef TARGET_PC
    /* Corrupted MAT3 entries can produce invalid block pointers/mode.
     * Rebuild a known-safe minimal material in that case. */
    if (mMaterialMode == 0 || mColorBlock == NULL || mTexGenBlock == NULL || mTevBlock == NULL ||
        mIndBlock == NULL || mPEBlock == NULL) {
        mMaterialMode = 1;
        mColorBlock = createColorBlock(0x40000000);
        mTexGenBlock = createTexGenBlock(0);
        mTevBlock = createTevBlock(1);
        mIndBlock = createIndBlock(0);
        mPEBlock = createPEBlock(0x10000000, 1);

        if (mColorBlock != NULL) {
            mColorBlock->setColorChanNum((u8)0);
            mColorBlock->setCullMode(GX_CULL_BACK);
        }
        if (mTexGenBlock != NULL) {
            mTexGenBlock->setTexGenNum((u32)0);
        }
        if (mTevBlock != NULL) {
            mTevBlock->setTevStageNum(1);
            mTevBlock->setTexNo(0, (u16)0xFFFF);
        }
    }

    /* Safe DL size computation — check each block's vtable is valid before
     * calling virtual countDLSize(). A corrupt BMD can leave blocks with
     * NULL vtables, causing a crash at the virtual dispatch. */
    auto safeCountBlock = [](void* block) -> u32 {
        if (!block) return 0;
        /* Check vtable pointer (first word of the object) isn't NULL */
        void* vtable = *(void**)block;
        if (!vtable) return 128; /* safe fallback size */
        /* Check vtable pointer is in a reasonable code range */
        uintptr_t vp = (uintptr_t)vtable;
        if (vp < 0x100000000ULL || vp > 0x200000000ULL) return 128;
        return 0; /* vtable looks valid, caller should use real method */
    };

    u32 dlSize = 0;
    u32 guard;
    guard = safeCountBlock(mColorBlock);
    dlSize += guard ? guard : mColorBlock->countDLSize();
    guard = safeCountBlock(mTexGenBlock);
    dlSize += guard ? guard : mTexGenBlock->countDLSize();
    guard = safeCountBlock(mTevBlock);
    dlSize += guard ? guard : mTevBlock->countDLSize();
    guard = safeCountBlock(mIndBlock);
    dlSize += guard ? guard : mIndBlock->countDLSize();
    guard = safeCountBlock(mPEBlock);
    dlSize += guard ? guard : mPEBlock->countDLSize();
    return (dlSize + 31) & ~0x1f;
#else
    u32 dlSize2 = (mColorBlock->countDLSize() + mTexGenBlock->countDLSize() + mTevBlock->countDLSize() +
                   mIndBlock->countDLSize() + mPEBlock->countDLSize() + 31) &
                  ~0x1f;
    return dlSize2;
#endif
}

void J3DMaterial::makeDisplayList_private(J3DDisplayListObj* pDLObj) {
    if (pDLObj == NULL) {
        return;
    }
#ifdef TARGET_PC
    if (mColorBlock == NULL || mTexGenBlock == NULL || mTevBlock == NULL || mIndBlock == NULL ||
        mPEBlock == NULL) {
        /* Keep runtime DL generation alive when MAT3 decode is incomplete. */
        mMaterialMode = 1;
        mColorBlock = createColorBlock(0x40000000);
        mTexGenBlock = createTexGenBlock(0);
        mTevBlock = createTevBlock(1);
        mIndBlock = createIndBlock(0);
        mPEBlock = createPEBlock(0x10000000, 1);
        mColorBlock->setColorChanNum((u8)0);
        mColorBlock->setCullMode(GX_CULL_BACK);
        mTexGenBlock->setTexGenNum((u32)0);
        mTevBlock->setTevStageNum(1);
    }
#endif
    const bool isSingleBuffer = (pDLObj->mpDisplayList[0] == pDLObj->mpDisplayList[1]);
    for (int attempt = 0; attempt < 5; ++attempt) {
#ifdef TARGET_PC
        pc_gd_clear_overflow_flag();
        /* Validate block vtables before calling virtual methods */
        auto vtableOk = [](void* p) -> bool {
            if (!p) return false;
            void* vt = *(void**)p;
            uintptr_t v = (uintptr_t)vt;
            return v >= 0x100000000ULL && v < 0x200000000ULL;
        };
        if (!vtableOk(mTevBlock) || !vtableOk(mIndBlock) || !vtableOk(mPEBlock) ||
            !vtableOk(mTexGenBlock) || !vtableOk(mColorBlock)) {
            return;
        }
#endif
        pDLObj->beginDL();
        mTevBlock->load();
        mIndBlock->load();
        mPEBlock->load();
        J3DGDSetGenMode(mTexGenBlock->getTexGenNum(), mColorBlock->getColorChanNum(),
                        mTevBlock->getTevStageNum(), mIndBlock->getIndTexStageNum(),
                        (GXCullMode)(u8)mColorBlock->getCullMode());
        mTexGenBlock->load();
        mColorBlock->load();
        J3DGDSetNumChans(mColorBlock->getColorChanNum());
        J3DGDSetNumTexGens(mTexGenBlock->getTexGenNum());
        pDLObj->endDL();
#ifdef TARGET_PC
        if (!pc_gd_consume_overflow_flag()) {
            return;
        }
        u32 nextSize = pDLObj->mMaxSize != 0 ? pDLObj->mMaxSize * 2 : 0x1000;
        if (isSingleBuffer) {
            if (pDLObj->newSingleDisplayList(nextSize) != kJ3DError_Success) {
                return;
            }
        } else if (pDLObj->newDisplayList(nextSize) != kJ3DError_Success) {
            return;
        }
#else
        return;
#endif
    }
}

void J3DMaterial::makeDisplayList() {
    J3DMatPacket* matPacket = j3dSys.getMatPacket();
    if (matPacket != NULL) {
        if (!matPacket->isLocked()) {
            matPacket->mDiffFlag = mDiffFlag;
            makeDisplayList_private(matPacket->getDisplayListObj());
        }
        return;
    }
#ifdef TARGET_PC
    if (mSharedDLObj != NULL) {
        makeDisplayList_private(mSharedDLObj);
    }
#endif
}

void J3DMaterial::makeSharedDisplayList() {
    makeDisplayList_private(mSharedDLObj);
}

void J3DMaterial::load() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (!j3dSys.checkFlag(2)) {
#ifdef TARGET_PC
        if (mTevBlock == NULL) {
            mTevBlock = createTevBlock(1);
        }
        if (mIndBlock == NULL) {
            mIndBlock = createIndBlock(0);
        }
        if (mPEBlock == NULL) {
            mPEBlock = createPEBlock(0, mMaterialMode ? mMaterialMode : 1);
        }
        if (mTexGenBlock == NULL) {
            mTexGenBlock = createTexGenBlock(0);
        }
        if (mColorBlock == NULL) {
            mColorBlock = createColorBlock(0);
        }
#endif
#ifdef TARGET_PC
        /* On PC, J3D material blocks write GX commands via J3DGDWrite* to
         * __GDCurrentDL. On GCN this points to the WGPIPE (hardware FIFO).
         * On PC, __GDCurrentDL is normally NULL, so all GDWrite calls would
         * crash. We set up a temporary buffer, let the blocks write into it,
         * then flush it through GXCallDisplayList to apply the state. */
        extern GDLObj* __GDCurrentDL;
        static u8 s_mat_gd_buf[4096] __attribute__((aligned(32)));
        GDLObj mat_gd;
        GDLObj* prev_gd = __GDCurrentDL;

        GDInitGDLObj(&mat_gd, s_mat_gd_buf, sizeof(s_mat_gd_buf));
        __GDCurrentDL = &mat_gd;

        jmp_buf matBuf;
        jmp_buf* prevMatBuf = pc_crash_get_jmpbuf();
        pc_crash_set_jmpbuf(&matBuf);
        if (setjmp(matBuf) == 0) {
            mTevBlock->load();
            mIndBlock->load();
            mPEBlock->load();
            J3DGDSetGenMode(mTexGenBlock->getTexGenNum(), mColorBlock->getColorChanNum(),
                            mTevBlock->getTevStageNum(), mIndBlock->getIndTexStageNum(),
                            (GXCullMode)(u8)mColorBlock->getCullMode());
            mTexGenBlock->load();
            mColorBlock->load();
            J3DGDSetNumChans(mColorBlock->getColorChanNum());
            J3DGDSetNumTexGens(mTexGenBlock->getTexGenNum());
            loadNBTScale(*mTexGenBlock->getNBTScale());

            /* Pre-load textures directly from J3DTexture before DL replay.
             * The GD buffer has GCN physical addresses that are meaningless on PC. */
            {
                J3DTexture* tex = j3dSys.getTexture();
                if (mTevBlock != NULL && tex != NULL) {
                    u8 nStages = mTevBlock->getTevStageNum();
                    static int s_preload_log = 0;
                    for (u8 s = 0; s < nStages && s < 8; s++) {
                        u16 texNo = mTevBlock->getTexNo(s);
                        /* Fix byte-swapped texNo from BE BMD data on LE platform */
                        if (texNo != 0xFFFF && texNo >= tex->getNum()) {
                            u16 swapped = (texNo >> 8) | (texNo << 8);
                            if (swapped < tex->getNum()) texNo = swapped;
                        }
                        if (s_preload_log < 30) {
                            fprintf(stderr, "[MAT-TEX] mat=%d stage=%d texNo=%d numTex=%d\n",
                                    mIndex, s, texNo, tex->getNum());
                            s_preload_log++;
                        }
                        if (texNo != 0xFFFF && texNo < tex->getNum()) {
                            tex->loadGX(texNo, (GXTexMapID)s);
                        }
                    }
                } else {
                    static int s_notex_log = 0;
                    if (s_notex_log < 10) {
                        fprintf(stderr, "[MAT-TEX] mat=%d NO TEX: tevBlock=%p tex=%p\n",
                                mIndex, (void*)mTevBlock, (void*)j3dSys.getTexture());
                        s_notex_log++;
                    }
                }
            }

            /* Flush the accumulated GD commands through the DL parser */
            u32 gd_size = GDGetCurrOffset();
            if (gd_size > 0) {
                static int s_mat_flush_log = 0;
                if (s_mat_flush_log < 10) {
                    fprintf(stderr, "[J3D-MAT] Flushing GD buf: %u bytes, tevStages=%u, chans=%u, texGens=%u, matIdx=%d\n",
                            gd_size, mTevBlock->getTevStageNum(), mColorBlock->getColorChanNum(),
                            mTexGenBlock->getTexGenNum(), mIndex);
                    /* Print first 32 bytes of the GD buffer */
                    fprintf(stderr, "[J3D-MAT] GD data:");
                    for (u32 bi = 0; bi < gd_size && bi < 64; bi++) {
                        fprintf(stderr, " %02x", s_mat_gd_buf[bi]);
                    }
                    fprintf(stderr, "\n");
                    s_mat_flush_log++;
                }
                GXCallDisplayList(s_mat_gd_buf, gd_size);
            }
        } else {
            static int s_matload_crash = 0;
            if (s_matload_crash++ < 5) {
                fprintf(stderr, "[J3D] material load crashed (addr=%p idx=%d), using defaults\n",
                        (void*)pc_crash_get_addr(), mIndex);
            }
            /* Set minimal default state so draws don't break */
            GXSetNumChans(1);
            GXSetNumTexGens(0);
            GXSetNumTevStages(1);
        }
        pc_crash_set_jmpbuf(prevMatBuf);
        __GDCurrentDL = prev_gd;
#else
        mTevBlock->load();
        mIndBlock->load();
        mPEBlock->load();
        J3DGDSetGenMode(mTexGenBlock->getTexGenNum(), mColorBlock->getColorChanNum(),
                        mTevBlock->getTevStageNum(), mIndBlock->getIndTexStageNum(),
                        (GXCullMode)(u8)mColorBlock->getCullMode());
        mTexGenBlock->load();
        mColorBlock->load();
        J3DGDSetNumChans(mColorBlock->getColorChanNum());
        J3DGDSetNumTexGens(mTexGenBlock->getTexGenNum());
        loadNBTScale(*mTexGenBlock->getNBTScale());
#endif
    }
}

void J3DMaterial::loadSharedDL() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (!j3dSys.checkFlag(2)) {
#ifdef TARGET_PC
        /* Pre-load textures before the material DL is replayed.
         * Material DLs contain GCN physical addresses for textures which
         * are meaningless on PC. Load textures directly from J3DTexture. */
        {
            J3DTexture* tex = j3dSys.getTexture();
            static int s_matdl_log = 0;
            if (s_matdl_log < 20) {
                fprintf(stderr, "[MAT-DL] loadSharedDL: tevBlock=%p tex=%p mode=%d\n",
                        (void*)mTevBlock, (void*)tex, mMaterialMode);
                s_matdl_log++;
            }
            if (mTevBlock != NULL && tex != NULL) {
                u8 nStages = mTevBlock->getTevStageNum();
                for (u8 s = 0; s < nStages && s < 8; s++) {
                    u16 texNo = mTevBlock->getTexNo(s);
                    if (texNo != 0xFFFF && texNo < tex->getNum()) {
                        if (s_matdl_log < 25) {
                            fprintf(stderr, "[MAT-DL]   stage %d texNo=%d/%d\n", s, texNo, tex->getNum());
                        }
                        tex->loadGX(texNo, (GXTexMapID)s);
                    }
                }
            }
        }
#endif
        mSharedDLObj->callDL();
        loadNBTScale(*mTexGenBlock->getNBTScale());
    }
}

void J3DMaterial::patch() {
    j3dSys.getMatPacket()->mDiffFlag = mDiffFlag;
    j3dSys.getMatPacket()->beginPatch();
    mTevBlock->patch();
    mColorBlock->patch();
    mTexGenBlock->patch();
    j3dSys.getMatPacket()->endPatch();
}

void J3DMaterial::diff(u32 diffFlags) {
    if (j3dSys.getMatPacket()->isEnabled_Diff()) {
        j3dSys.getMatPacket()->beginDiff();

        mTevBlock->diff(diffFlags);
        mIndBlock->diff(diffFlags);
        mPEBlock->diff(diffFlags);
        if (diffFlags & J3DDiffFlag_KonstColor) {
            J3DGDSetGenMode_3Param(mTexGenBlock->getTexGenNum(), mTevBlock->getTevStageNum(), mIndBlock->getIndTexStageNum());
            J3DGDSetNumTexGens(mTexGenBlock->getTexGenNum());
        }
        mTexGenBlock->diff(diffFlags);
        mColorBlock->diff(diffFlags);

        j3dSys.getMatPacket()->endDiff();
    }
}

void J3DMaterial::calc(f32 const (*param_0)[4]) {
    if (j3dSys.checkFlag(0x40000000)) {
        mTexGenBlock->calcPostTexMtx(param_0);
    } else {
        mTexGenBlock->calc(param_0);
    }

    calcCurrentMtx();
    setCurrentMtx();
}

void J3DMaterial::calcDiffTexMtx(f32 const (*param_0)[4]) {
    if (j3dSys.checkFlag(0x40000000)) {
        mTexGenBlock->calcPostTexMtxWithoutViewMtx(param_0);
    } else {
        mTexGenBlock->calcWithoutViewMtx(param_0);
    }
}

void J3DMaterial::setCurrentMtx() {
    mShape->setCurrentMtx(mCurrentMtx);
}

void J3DMaterial::calcCurrentMtx() {
    if (!j3dSys.checkFlag(0x40000000)) {
        mCurrentMtx.setCurrentTexMtx(
            getTexCoord(0)->getTexGenMtx(),
            getTexCoord(1)->getTexGenMtx(),
            getTexCoord(2)->getTexGenMtx(),
            getTexCoord(3)->getTexGenMtx(),
            getTexCoord(4)->getTexGenMtx(),
            getTexCoord(5)->getTexGenMtx(),
            getTexCoord(6)->getTexGenMtx(),
            getTexCoord(7)->getTexGenMtx()
        );
    } else {
        mCurrentMtx.setCurrentTexMtx(
            getTexCoord(0)->getTexMtxReg(),
            getTexCoord(1)->getTexMtxReg(),
            getTexCoord(2)->getTexMtxReg(),
            getTexCoord(3)->getTexMtxReg(),
            getTexCoord(4)->getTexMtxReg(),
            getTexCoord(5)->getTexMtxReg(),
            getTexCoord(6)->getTexMtxReg(),
            getTexCoord(7)->getTexMtxReg()
        );
    }
}

void J3DMaterial::copy(J3DMaterial* pOther) {
    J3D_ASSERT_NULLPTR(620, pOther != NULL);
    mColorBlock->reset(pOther->mColorBlock);
    mTexGenBlock->reset(pOther->mTexGenBlock);
    mTevBlock->reset(pOther->mTevBlock);
    mIndBlock->reset(pOther->mIndBlock);
    mPEBlock->reset(pOther->mPEBlock);
}

void J3DMaterial::reset() {
    if ((~mDiffFlag & J3DDiffFlag_Changed) == 0) {
        mDiffFlag &= ~J3DDiffFlag_Changed;
        mMaterialMode = mpOrigMaterial->mMaterialMode;
        mInvalid = mpOrigMaterial->mInvalid;
        mMaterialAnm = NULL;
        copy(mpOrigMaterial);
    }
}

void J3DMaterial::change() {
    if ((mDiffFlag & (J3DDiffFlag_Changed | J3DDiffFlag_Unk40000000)) == 0) {
        mDiffFlag |= J3DDiffFlag_Changed;
        mMaterialAnm = NULL;
    }
}

s32 J3DMaterial::newSharedDisplayList(u32 dlSize) {
    if (mSharedDLObj == NULL) {
        mSharedDLObj = new J3DDisplayListObj();
        if (mSharedDLObj == NULL) {
            return kJ3DError_Alloc;
        }

        s32 ret = mSharedDLObj->newDisplayList(dlSize);
        if (ret != kJ3DError_Success) {
            return ret;
        }
    }

    return kJ3DError_Success;
}

s32 J3DMaterial::newSingleSharedDisplayList(u32 dlSize) {
    if (mSharedDLObj == NULL) {
        mSharedDLObj = new J3DDisplayListObj();
        if (mSharedDLObj == NULL) {
            return kJ3DError_Alloc;
        }

        s32 ret = mSharedDLObj->newSingleDisplayList(dlSize);
        if (ret != kJ3DError_Success) {
            return ret;
        }
    }

    return kJ3DError_Success;
}

void J3DPatchedMaterial::initialize() {
    J3DMaterial::initialize();
}

void J3DPatchedMaterial::makeDisplayList() {}

void J3DPatchedMaterial::makeSharedDisplayList() {}

void J3DPatchedMaterial::load() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (j3dSys.checkFlag(2)) {
        return;
    }
}

void J3DPatchedMaterial::loadSharedDL() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (!j3dSys.checkFlag(0x02)) {
#ifdef TARGET_PC
        if (mTevBlock != NULL && j3dSys.getTexture() != NULL) {
            J3DTexture* tex = j3dSys.getTexture();
            u8 nStages = mTevBlock->getTevStageNum();
            for (u8 s = 0; s < nStages && s < 8; s++) {
                u16 texNo = mTevBlock->getTexNo(s);
                if (texNo != 0xFFFF && texNo < tex->getNum()) {
                    tex->loadGX(texNo, (GXTexMapID)s);
                }
            }
        }
#endif
        mSharedDLObj->callDL();
    }
}

void J3DPatchedMaterial::reset() {}

void J3DPatchedMaterial::change() {}

void J3DLockedMaterial::initialize() {
    J3DMaterial::initialize();
}

void J3DLockedMaterial::makeDisplayList() {}

void J3DLockedMaterial::makeSharedDisplayList() {}

void J3DLockedMaterial::load() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (j3dSys.checkFlag(2)) {
        return;
    }
}

void J3DLockedMaterial::loadSharedDL() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (!j3dSys.checkFlag(0x02)) {
#ifdef TARGET_PC
        if (mTevBlock != NULL && j3dSys.getTexture() != NULL) {
            J3DTexture* tex = j3dSys.getTexture();
            u8 nStages = mTevBlock->getTevStageNum();
            for (u8 s = 0; s < nStages && s < 8; s++) {
                u16 texNo = mTevBlock->getTexNo(s);
                if (texNo != 0xFFFF && texNo < tex->getNum()) {
                    tex->loadGX(texNo, (GXTexMapID)s);
                }
            }
        }
#endif
        mSharedDLObj->callDL();
    }
}

void J3DLockedMaterial::patch() {}

void J3DLockedMaterial::diff(u32 diffFlags) {}

void J3DLockedMaterial::calc(const Mtx param_0) {}

void J3DLockedMaterial::reset() {}

void J3DLockedMaterial::change() {}
