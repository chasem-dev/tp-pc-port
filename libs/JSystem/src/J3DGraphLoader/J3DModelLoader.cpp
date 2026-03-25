#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/J3DGraphLoader/J3DModelLoader.h"
#include "JSystem/J3DGraphAnimator/J3DAnimation.h"
#include "JSystem/J3DGraphAnimator/J3DShapeTable.h"
#include "JSystem/J3DGraphAnimator/J3DJointTree.h"
#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphLoader/J3DJointFactory.h"
#include "JSystem/J3DGraphLoader/J3DMaterialFactory.h"
#include "JSystem/J3DGraphLoader/J3DMaterialFactory_v21.h"
#include "JSystem/J3DGraphLoader/J3DShapeFactory.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphBase/J3DMaterial.h"
#include "JSystem/JUtility/JUTNameTab.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JSupport/JSupport.h"
#ifdef TARGET_PC
#include "pc_j3d_bswap.h"
extern int g_pc_verbose;

static bool pc_j3d_isKnownBlockType(uint32_t type) {
    switch (type) {
    case 'INF1':
    case 'VTX1':
    case 'EVP1':
    case 'DRW1':
    case 'JNT1':
    case 'MAT3':
    case 'MAT2':
    case 'SHP1':
    case 'TEX1':
    case 'MDL3':
        return true;
    default:
        return false;
    }
}

static bool pc_j3d_tryRepairBlock(const J3DModelFileData* data, const J3DModelBlock*& block,
                                  u32 block_no, uintptr_t file_start, uintptr_t file_end) {
    uintptr_t block_addr = (uintptr_t)block;
    if (block_addr < file_start + 0x20 || block_addr + sizeof(J3DModelBlock) > file_end) {
        return false;
    }

    /* If the block type is already a known type in its current byte order,
     * the block was already correctly swapped by bswap_file — don't double-swap. */
    if (pc_j3d_isKnownBlockType(block->mBlockType)) {
        return false;
    }
    {
        static int s_rep_log = 0;
        if (s_rep_log++ < 10) {
            fprintf(stderr, "[J3D] tryRepair: block_no=%u type=0x%08x size=0x%08x addr=%p\n",
                    block_no, block->mBlockType, block->mBlockSize, (const void*)block);
        }
    }

    uint32_t swappedType = pc_bswap32(block->mBlockType);
    uint32_t swappedSize = pc_bswap32(block->mBlockSize);
    if (!pc_j3d_isKnownBlockType(swappedType) || swappedSize < sizeof(J3DModelBlock) ||
        block_addr + swappedSize > file_end) {
        return false;
    }

    J3DModelBlock* mutableBlock = const_cast<J3DModelBlock*>(block);
    mutableBlock->mBlockType = swappedType;
    mutableBlock->mBlockSize = swappedSize;
    pc_j3d_bswap_block(swappedType, reinterpret_cast<uint8_t*>(mutableBlock), swappedSize);
    block = mutableBlock;

    fprintf(stderr,
            "[J3D] repaired late-swapped block: block_no=%u type=0x%08x '%c%c%c%c' size=0x%x raw=%p\n",
            block_no, swappedType, (char)(swappedType >> 24), (char)(swappedType >> 16),
            (char)(swappedType >> 8), (char)swappedType, swappedSize, (const void*)block);
    fflush(stderr);
    return true;
}

static const char* pc_j3d_hierarchy_type_name(uint16_t type) {
    switch (type) {
    case 0x00:
        return "END";
    case 0x01:
        return "BEGIN_CHILD";
    case 0x02:
        return "END_CHILD";
    case 0x10:
        return "JOINT";
    case 0x11:
        return "MATERIAL";
    case 0x12:
        return "SHAPE";
    default:
        return "UNKNOWN";
    }
}
#endif

J3DModelLoader::J3DModelLoader() :
                mpModelData(NULL),
                mpMaterialTable(NULL),
                mpShapeBlock(NULL),
                mpMaterialBlock(NULL),
                mpModelHierarchy(NULL),
                field_0x18(0),
                mEnvelopeSize(0) {
    /* empty function */
}

J3DModelData* J3DModelLoaderDataBase::load(void const* i_data, u32 i_flags, u32 i_dataSizeLimit) {
    J3D_ASSERT_NULLPTR(52, i_data);
    if (i_data == NULL) {
        return NULL;
    }
#ifdef TARGET_PC
    {
        uint32_t headerSize = 0;
        const char* reason = "ok";
        uint32_t swapSize = pc_j3d_get_safe_swap_size(i_data, i_dataSizeLimit, &headerSize, &reason);
        if (swapSize > 0) {
            if (g_pc_verbose && (i_dataSizeLimit != 0 || headerSize != swapSize)) {
                fprintf(stderr,
                        "[J3D] model swap size: header=%u limit=%u effective=%u reason=%s ptr=%p\n",
                        headerSize, i_dataSizeLimit, swapSize, reason, i_data);
            }
            pc_j3d_bswap_file(const_cast<void*>(i_data), swapSize);
        } else if (g_pc_verbose) {
            fprintf(stderr,
                    "[J3D] model swap skipped: header=%u limit=%u reason=%s ptr=%p\n",
                    headerSize, i_dataSizeLimit, reason, i_data);
        }
    }
#endif
    const J3DModelFileData* header = (const J3DModelFileData*)i_data;
    if (header->mMagic1 == 'J3D1' && header->mMagic2 == 'bmd1') {
        JUT_PANIC(64, "Error : version error.");
        return NULL;
    }
    if (header->mMagic1 == 'J3D2' && header->mMagic2 == 'bmd2') {
        J3DModelLoader_v21 loader;
        return loader.load(i_data, i_flags);
    }
    if (header->mMagic1 == 'J3D2' && header->mMagic2 == 'bmd3') {
        J3DModelLoader_v26 loader;
        return loader.load(i_data, i_flags);
    }
    JUT_PANIC(89, "Error : version error.");
    return NULL;
}

J3DModelData* J3DModelLoaderDataBase::loadBinaryDisplayList(const void* i_data, u32 flags,
                                                            u32 i_dataSizeLimit) {
    J3D_ASSERT_NULLPTR(138, i_data);
    if (!i_data) {
        return NULL;
    }
#ifdef TARGET_PC
    {
        uint32_t headerSize = 0;
        const char* reason = "ok";
        uint32_t swapSize = pc_j3d_get_safe_swap_size(i_data, i_dataSizeLimit, &headerSize, &reason);
        if (swapSize > 0) {
            if (g_pc_verbose && (i_dataSizeLimit != 0 || headerSize != swapSize)) {
                fprintf(stderr,
                        "[J3D] bdl swap size: header=%u limit=%u effective=%u reason=%s ptr=%p\n",
                        headerSize, i_dataSizeLimit, swapSize, reason, i_data);
            }
            pc_j3d_bswap_file(const_cast<void*>(i_data), swapSize);
        } else if (g_pc_verbose) {
            fprintf(stderr, "[J3D] bdl swap skipped: header=%u limit=%u reason=%s ptr=%p\n",
                    headerSize, i_dataSizeLimit, reason, i_data);
        }
    }
#endif
    const J3DModelFileData* header = (const J3DModelFileData*)i_data;
    if (header->mMagic1 == 'J3D2' && (header->mMagic2 == 'bdl3' || header->mMagic2 == 'bdl4')) {
        J3DModelLoader_v26 loader;
        return loader.loadBinaryDisplayList(i_data, flags);
    }
    JUT_PANIC(157, "Error : version error.");
    return NULL;
}

J3DModelData* J3DModelLoader::load(void const* i_data, u32 i_flags) {
    s32 freeSize = JKRGetCurrentHeap()->getTotalFreeSize();
    mpModelData = new J3DModelData();
    J3D_ASSERT_ALLOCMEM(177, mpModelData);
    mpModelData->clear();
    mpModelData->mpRawData = i_data;
    mpModelData->setModelDataType(0);
    mpMaterialTable = &mpModelData->mMaterialTable;
    J3DModelFileData const* data = (J3DModelFileData*)i_data;
    J3DModelBlock const* block = data->mBlocks;
#ifdef TARGET_PC
    uintptr_t file_start = (uintptr_t)i_data;
    uint32_t file_size = ((const uint32_t*)i_data)[2];
    uintptr_t file_end = file_start + file_size;
    static int s_mdl = 0;
    bool forceLog = (file_size == 0x1e72c0 || file_size == 0x1d40 || file_size == 0x58e0 ||
                     file_size == 0x28bc0 || file_size == 0x28a0 || file_size == 0x3a7e0);
    bool doLog = forceLog || (s_mdl++ < 64);
    if (doLog) {
        fprintf(stderr, "[J3D] load: %d blocks, flags=0x%x size=0x%x base=%p\n",
                data->mBlockNum, i_flags, file_size, i_data);
        fflush(stderr);
    }
#endif
    for (u32 block_no = 0; block_no < data->mBlockNum; block_no++) {
#ifdef TARGET_PC
        uintptr_t block_addr = (uintptr_t)block;
        if (block_addr < file_start + 0x20 || block_addr + sizeof(J3DModelBlock) > file_end) {
            fprintf(stderr,
                    "[J3D] block pointer out of range: block_no=%u block=%p file=[%p,%p) size=0x%x\n",
                    block_no, (const void*)block, (const void*)file_start, (const void*)file_end,
                    ((const uint32_t*)i_data)[2]);
            fflush(stderr);
            return NULL;
        }
        bool blockLooksBroken =
            (block->mBlockSize < sizeof(J3DModelBlock) || block_addr + block->mBlockSize > file_end ||
             !pc_j3d_isKnownBlockType(block->mBlockType));
        if (blockLooksBroken) {
            const J3DModelBlock* repairedBlock = block;
            if (pc_j3d_tryRepairBlock(data, repairedBlock, block_no, file_start, file_end)) {
                block = repairedBlock;
                block_addr = (uintptr_t)block;
            }
        }

        if (doLog) {
            fprintf(stderr, "[J3D]   block %d @ +0x%zx: type=0x%08x '%c%c%c%c' size=%d\n",
                    block_no, block_addr - file_start,
                    block->mBlockType,
                    (char)(block->mBlockType>>24), (char)(block->mBlockType>>16),
                    (char)(block->mBlockType>>8), (char)block->mBlockType,
                    block->mBlockSize);
            fflush(stderr);
        }
        if (block->mBlockSize < sizeof(J3DModelBlock) || block_addr + block->mBlockSize > file_end) {
            fprintf(stderr,
                    "[J3D] invalid block bounds: block_no=%u type=0x%08x size=0x%x block=%p file=[%p,%p)\n",
                    block_no, block->mBlockType, block->mBlockSize, (const void*)block,
                    (const void*)file_start, (const void*)file_end);
            fflush(stderr);
            return NULL;
        }
#endif
        switch (block->mBlockType) {
            case 'INF1':
#ifdef TARGET_PC
                if (doLog) { fprintf(stderr, "[J3D]   -> readInformation\n"); fflush(stderr); }
                if (forceLog) {
                    const u32* infWords = reinterpret_cast<const u32*>(block);
                    fprintf(stderr,
                            "[J3D]   INF1 words:"
                            " %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
                            infWords[0], infWords[1], infWords[2], infWords[3], infWords[4],
                            infWords[5], infWords[6], infWords[7], infWords[8], infWords[9],
                            infWords[10], infWords[11]);
                    fflush(stderr);
                }
#endif
                readInformation((J3DModelInfoBlock*)block, (s32)i_flags);
                break;
            case 'VTX1':
#ifdef TARGET_PC
                if (doLog) { fprintf(stderr, "[J3D]   -> readVertex\n"); fflush(stderr); }
#endif
                readVertex((J3DVertexBlock*)block);
                break;
            case 'EVP1':
#ifdef TARGET_PC
                if (doLog) { fprintf(stderr, "[J3D]   -> readEnvelop\n"); fflush(stderr); }
#endif
                readEnvelop((J3DEnvelopeBlock*)block);
                break;
            case 'DRW1':
#ifdef TARGET_PC
                if (doLog) { fprintf(stderr, "[J3D]   -> readDraw\n"); fflush(stderr); }
#endif
                readDraw((J3DDrawBlock*)block);
                break;
            case 'JNT1':
#ifdef TARGET_PC
                if (doLog) { fprintf(stderr, "[J3D]   -> readJoint\n"); fflush(stderr); }
#endif
                readJoint((J3DJointBlock*)block);
                break;
            case 'MAT3':
#ifdef TARGET_PC
                if (doLog) { fprintf(stderr, "[J3D]   -> readMaterial\n"); fflush(stderr); }
#endif
                readMaterial((J3DMaterialBlock*)block, (s32)i_flags);
                break;
            case 'MAT2':
                readMaterial_v21((J3DMaterialBlock_v21*)block, (s32)i_flags);
                break;
            case 'SHP1':
#ifdef TARGET_PC
                if (doLog) { fprintf(stderr, "[J3D]   -> readShape\n"); fflush(stderr); }
#endif
                readShape((J3DShapeBlock*)block, (s32)i_flags);
                break;
            case 'TEX1':
                readTexture((J3DTextureBlock*)block);
                break;
            default:
#ifdef TARGET_PC
                fprintf(stderr,
                        "[J3D] unknown block: block_no=%u type=0x%08x '%c%c%c%c' size=%u raw=%p\n",
                        block_no, block->mBlockType,
                        (char)(block->mBlockType >> 24), (char)(block->mBlockType >> 16),
                        (char)(block->mBlockType >> 8), (char)block->mBlockType,
                        block->mBlockSize, (const void*)block);
                fflush(stderr);
                return NULL;
#else
                OSReport("Unknown data block\n");
                break;
#endif
        }

        block = (J3DModelBlock*)((uintptr_t)block + block->mBlockSize);
    }
#ifdef TARGET_PC
    if (doLog) {
        const J3DJointTree& jointTree = mpModelData->getJointTree();
        const J3DDrawMtxData* drawMtxData = mpModelData->getDrawMtxData();
        fprintf(stderr,
                "[J3D] post blocks: jointNum=%u shapeNum=%u materialNum=%u wevlpNum=%u"
                " mixNumPtr=%p mixIdxPtr=%p mixWgtPtr=%p impIdxPtr=%p"
                " drawEntryNum=%u drawFull=%u drawFlagPtr=%p drawIdxPtr=%p hierarchy=%p\n",
                jointTree.mJointNum, mpModelData->getShapeNum(), mpModelData->getMaterialNum(),
                jointTree.mWEvlpMtxNum,
                (void*)jointTree.mWEvlpMixMtxNum, (void*)jointTree.mWEvlpMixMtxIndex,
                (void*)jointTree.mWEvlpMixWeight, (void*)jointTree.mWEvlpImportantMtxIdx,
                drawMtxData->mEntryNum, drawMtxData->mDrawFullWgtMtxNum,
                (void*)drawMtxData->mDrawMtxFlag, (void*)drawMtxData->mDrawMtxIndex,
                (void*)mpModelData->getHierarchy());
        fflush(stderr);
    }

    if (forceLog) {
        const J3DModelHierarchy* dbgHierarchy = mpModelData->getHierarchy();
        for (u32 i = 0; dbgHierarchy != NULL && i < 64; i++) {
            fprintf(stderr, "[J3D] hierarchy[%u]: type=0x%04x %s value=%u\n",
                    i, dbgHierarchy[i].mType, pc_j3d_hierarchy_type_name(dbgHierarchy[i].mType),
                    dbgHierarchy[i].mValue);
            if (dbgHierarchy[i].mType == 0x00) {
                break;
            }
        }
        fflush(stderr);
    }
#endif
    J3DModelHierarchy const* hierarchy = mpModelData->getHierarchy();
#ifdef TARGET_PC
    if (doLog) {
        fprintf(stderr, "[J3D] makeHierarchy begin\n");
        fflush(stderr);
    }
#endif
    mpModelData->makeHierarchy(NULL, &hierarchy);
#ifdef TARGET_PC
    if (doLog) {
        fprintf(stderr, "[J3D] makeHierarchy done\n");
        fflush(stderr);
        if (forceLog) {
            for (u16 i = 0; i < mpModelData->getMaterialNum(); i++) {
                J3DMaterial* material = mpModelData->getMaterialNodePointer(i);
                J3DShape* shape = material != NULL ? material->getShape() : NULL;
                J3DJoint* joint = material != NULL ? material->getJoint() : NULL;
                fprintf(stderr,
                        "[J3D] material[%u]: material=%p joint=%p jointNo=%d shape=%p shapeIdx=%d next=%p\n",
                        i, (void*)material, (void*)joint, joint != NULL ? joint->getJntNo() : -1,
                        (void*)shape, shape != NULL ? shape->getIndex() : -1,
                        material != NULL ? (void*)material->getNext() : NULL);
            }

            for (u16 i = 0; i < mpModelData->getJointNum(); i++) {
                J3DJoint* joint = mpModelData->getJointNodePointer(i);
                fprintf(stderr, "[J3D] joint[%u]: joint=%p child=%p younger=%p mesh=%p\n", i,
                        (void*)joint, joint != NULL ? (void*)joint->getChild() : NULL,
                        joint != NULL ? (void*)joint->getYounger() : NULL,
                        joint != NULL ? (void*)joint->getMesh() : NULL);
                J3DMaterial* mesh = joint != NULL ? joint->getMesh() : NULL;
                for (u32 chain = 0; mesh != NULL && chain < 8; chain++, mesh = mesh->getNext()) {
                    J3DShape* shape = mesh->getShape();
                    fprintf(stderr,
                            "[J3D]   joint[%u].mesh[%u]: material=%p idx=%u shape=%p shapeIdx=%d next=%p\n",
                            i, chain, (void*)mesh, mesh->getIndex(), (void*)shape,
                            shape != NULL ? shape->getIndex() : -1, (void*)mesh->getNext());
                }
            }
            fflush(stderr);
        }
        fprintf(stderr, "[J3D] sortVcdVatCmd begin\n");
        fflush(stderr);
    }
#endif
    mpModelData->getShapeTable()->sortVcdVatCmd();
#ifdef TARGET_PC
    if (doLog) {
        fprintf(stderr, "[J3D] sortVcdVatCmd done\n");
        fflush(stderr);
        fprintf(stderr, "[J3D] findImportantMtxIndex begin\n");
        fflush(stderr);
    }
#endif
    mpModelData->getJointTree().findImportantMtxIndex();
#ifdef TARGET_PC
    if (doLog) {
        fprintf(stderr, "[J3D] findImportantMtxIndex done\n");
        fflush(stderr);
        fprintf(stderr, "[J3D] setupBBoardInfo begin\n");
        fflush(stderr);
    }
#endif
    setupBBoardInfo();
#ifdef TARGET_PC
    if (doLog) {
        fprintf(stderr, "[J3D] setupBBoardInfo done\n");
        fflush(stderr);
    }
#endif
    if (mpModelData->getFlag() & 0x100) {
        for (u16 shape_no = 0; shape_no < mpModelData->getShapeNum(); shape_no++) {
            J3DShape* shape = mpModelData->getShapeNodePointer(shape_no);
#ifdef TARGET_PC
            if (shape == NULL) {
                fprintf(stderr, "[J3D] WARNING: shape %d/%d is NULL (flag=0x%x) — skipping onFlag\n",
                        shape_no, mpModelData->getShapeNum(), mpModelData->getFlag());
                continue;
            }
#endif
            shape->onFlag(0x200);
        }
    }
    return mpModelData;
}


J3DMaterialTable* J3DModelLoader::loadMaterialTable(void const* i_data) {
    int flags = 0x51100000;
    mpMaterialTable = new J3DMaterialTable();
    J3D_ASSERT_ALLOCMEM(279, mpMaterialTable);
    mpMaterialTable->clear();
    J3DModelFileData const* data = (J3DModelFileData*)i_data;
    J3DModelBlock const* block = data->mBlocks;
    for (u32 block_no = 0; block_no < data->mBlockNum; block_no++) {
        switch (block->mBlockType) {
            case 'MAT3':
                readMaterialTable((J3DMaterialBlock*)block, flags);
                break;
            case 'MAT2':
                readMaterialTable_v21((J3DMaterialBlock_v21*)block, flags);
                break;
            case 'TEX1':
                readTextureTable((J3DTextureBlock*)block);
                break;
            default:
                OSReport("Unknown data block\n");
                break;
        }
        block = (J3DModelBlock*)((uintptr_t)block + block->mBlockSize);
    }
    if (mpMaterialTable->mTexture == NULL) {
        mpMaterialTable->mTexture = new J3DTexture(0, NULL);
        J3D_ASSERT_ALLOCMEM(319, mpMaterialTable->mTexture);
    }
    return mpMaterialTable;
}

inline u32 getBdlFlag_MaterialType(u32 flags) {
    return flags & (J3DMLF_13 | J3DMLF_DoBdlMaterialCalc);
}

J3DModelData* J3DModelLoader::loadBinaryDisplayList(void const* i_data, u32 i_flags) {
    mpModelData = new J3DModelData();
    J3D_ASSERT_ALLOCMEM(338, mpModelData);
    mpModelData->clear();
    mpModelData->mpRawData = i_data;
    mpModelData->setModelDataType(1);
    mpMaterialTable = &mpModelData->mMaterialTable;
    J3DModelFileData const* data = (J3DModelFileData*)i_data;
    J3DModelBlock const* block = data->mBlocks;
    for (u32 block_no = 0; block_no < data->mBlockNum; block_no++) {
        s32 flags;
        u32 materialType;
        switch (block->mBlockType) {
            case 'INF1':
                flags = i_flags;
                readInformation((J3DModelInfoBlock*)block, flags);
                break;
            case 'VTX1':
                readVertex((J3DVertexBlock*)block);
                break;
            case 'EVP1':
                readEnvelop((J3DEnvelopeBlock*)block);
                break;
            case 'DRW1':
                readDraw((J3DDrawBlock*)block);
                break;
            case 'JNT1':
                readJoint((J3DJointBlock*)block);
                break;
            case 'SHP1':
                readShape((J3DShapeBlock*)block, i_flags);
                break;
            case 'TEX1':
                readTexture((J3DTextureBlock*)block);
                break;
            case 'MDL3':
                readMaterialDL((J3DMaterialDLBlock*)block, i_flags);
                modifyMaterial(i_flags);
                break;
            case 'MAT3':
                flags = 0x50100000;
                flags |= (i_flags & 0x3000000);
                mpMaterialBlock = (J3DMaterialBlock*)block;
                materialType = getBdlFlag_MaterialType(i_flags);
                if (materialType == 0) {
                    readMaterial((J3DMaterialBlock*)block, flags);
                } else if (materialType == 0x2000) {
                    readPatchedMaterial((J3DMaterialBlock*)block, flags);
                }
                break;
            default:
                OSReport("Unknown data block\n");
                break;
        }
        block = (J3DModelBlock*)((uintptr_t)block + block->mBlockSize);
    }
    J3DModelHierarchy const* hierarchy = mpModelData->getHierarchy();
    mpModelData->makeHierarchy(NULL, &hierarchy);
    mpModelData->getShapeTable()->sortVcdVatCmd();
    mpModelData->getJointTree().findImportantMtxIndex();
    setupBBoardInfo();
    mpModelData->indexToPtr();
    return mpModelData;
}

void J3DModelLoader::setupBBoardInfo() {
#ifdef TARGET_PC
    // Billboard tagging still relies on GC-era joint/shadow assumptions that are not stable on the
    // current 64-bit PC loader path. Skip the post-pass so model loading can complete; billboard
    // behavior can be restored once the joint-node bookkeeping is hardened.
    return;
#endif

    if (mpShapeBlock == NULL) {
        return;
    }

    u16* index_table = JSUConvertOffsetToPtr<u16>(mpShapeBlock,
                                                  (uintptr_t)mpShapeBlock->mpIndexTable);
    J3DShapeInitData* shape_init_data =
        JSUConvertOffsetToPtr<J3DShapeInitData>(mpShapeBlock,
                                               (uintptr_t)mpShapeBlock->mpShapeInitData);

    if (index_table == NULL || shape_init_data == NULL) {
#ifdef TARGET_PC
        fprintf(stderr,
                "[J3D] setupBBoardInfo: missing shape metadata (shapeBlock=%p indexTable=%p initData=%p)\n",
                (void*)mpShapeBlock, (void*)index_table, (void*)shape_init_data);
        fflush(stderr);
#endif
        return;
    }

    for (u16 i = 0; i < mpModelData->getJointNum(); i++) {
        J3DJoint* joint = mpModelData->getJointNodePointer(i);
        if (joint == NULL) {
            continue;
        }

        J3DMaterial* mesh = joint->getMesh();
        if (mesh != NULL) {
            J3DShape* shape = mesh->getShape();
            if (shape == NULL) {
#ifdef TARGET_PC
                fprintf(stderr, "[J3D] setupBBoardInfo: joint %u has mesh %p with no shape yet\n",
                        i, (void*)mesh);
                fflush(stderr);
#endif
                continue;
            }
            u32 shape_index = shape->getIndex();
            if (shape_index >= mpModelData->getShapeNum()) {
#ifdef TARGET_PC
                fprintf(stderr,
                        "[J3D] setupBBoardInfo: joint %u shape index %u out of range (shapeNum=%u)\n",
                        i, shape_index, mpModelData->getShapeNum());
                fflush(stderr);
#endif
                continue;
            }
            J3DShapeInitData* r26 = &shape_init_data[index_table[shape_index]];
            switch (r26->mShapeMtxType) {
                case 0:
                    joint->setMtxType(0);
                    break;
                case 1:
                    joint->setMtxType(1);
                    mpModelData->mbHasBillboard = true;
                    break;
                case 2:
                    joint->setMtxType(2);
                    mpModelData->mbHasBillboard = true;
                    break;
                case 3:
                    joint->setMtxType(0);
                    break;
                default:
                    OSReport("WRONG SHAPE MATRIX TYPE (__FILE__)\n");
                    break;
            }
        }
    }
}

void J3DModelLoader::readInformation(J3DModelInfoBlock const* i_block, u32 i_flags) {
    J3D_ASSERT_NULLPTR(506, i_block);
#ifdef TARGET_PC
    {
        const u8* raw = (const u8*)i_block;
        fprintf(stderr, "[J3D] readInformation: i_flags=0x%x blockFlags=0x%04x raw[8..13]=%02x%02x%02x%02x%02x%02x\n",
                i_flags, i_block->mFlags,
                raw[8], raw[9], raw[10], raw[11], raw[12], raw[13]);
        fflush(stderr);
    }
#endif
    mpModelData->mFlags = i_flags | i_block->mFlags;
#ifdef TARGET_PC
    /* Fix double-swapped mFlags: detect when the u16 pair at +8/+A was swapped
     * as a u32 instead of two u16s. When mFlags=0xFFFF, the real value is at +A. */
    if ((i_block->mFlags & 0xFFFF) == 0xFFFF) {
        const u8* raw = (const u8*)i_block;
        u16 realFlags = *(const u16*)(raw + 0x0A);
        mpModelData->mFlags = i_flags | realFlags;
    }
#endif
    mpModelData->getJointTree().setFlag(mpModelData->mFlags);
    J3DMtxCalc* mtx_calc = NULL;
    switch (mpModelData->mFlags & 0xf) {
        case 0:
            mtx_calc = new J3DMtxCalcNoAnm<J3DMtxCalcCalcTransformBasic,J3DMtxCalcJ3DSysInitBasic>();
            break;
        case 1:
            mtx_calc = new J3DMtxCalcNoAnm<J3DMtxCalcCalcTransformSoftimage,J3DMtxCalcJ3DSysInitSoftimage>();
            break;
        case 2:
            mtx_calc = new J3DMtxCalcNoAnm<J3DMtxCalcCalcTransformMaya,J3DMtxCalcJ3DSysInitMaya>();
            break;
        default:
            JUT_PANIC(529, "Error : Invalid MtxCalcType.");
            break;
    }
    J3D_ASSERT_ALLOCMEM(532, mtx_calc);
    mpModelData->setBasicMtxCalc(mtx_calc);
    mpModelData->getVertexData().mPacketNum = i_block->mPacketNum;
    mpModelData->getVertexData().mVtxNum = i_block->mVtxNum;
    mpModelData->setHierarchy(JSUConvertOffsetToPtr<J3DModelHierarchy>(i_block, i_block->mpHierarchy));
}

static _GXCompType getFmtType(_GXVtxAttrFmtList* i_fmtList, _GXAttr i_attr) {
    for (; i_fmtList->attr != GX_VA_NULL; i_fmtList++) {
        if (i_fmtList->attr == i_attr) {
            return i_fmtList->type;
        }
    }
    return GX_F32;
}

void J3DModelLoader::readVertex(J3DVertexBlock const* i_block) {
    J3D_ASSERT_NULLPTR(577, i_block);
    J3DVertexData& vertex_data = mpModelData->getVertexData();
    vertex_data.mVtxAttrFmtList =
        JSUConvertOffsetToPtr<GXVtxAttrFmtList>(i_block, i_block->mpVtxAttrFmtList);
    vertex_data.mVtxPosArray = JSUConvertOffsetToPtr<void>(i_block, i_block->mpVtxPosArray);
    vertex_data.mVtxNrmArray = JSUConvertOffsetToPtr<void>(i_block, i_block->mpVtxNrmArray);
    vertex_data.mVtxNBTArray = JSUConvertOffsetToPtr<void>(i_block, i_block->mpVtxNBTArray);
    for (int i = 0; i < 2; i++) {
        vertex_data.mVtxColorArray[i] =
            (GXColor*)JSUConvertOffsetToPtr<void>(i_block, i_block->mpVtxColorArray[i]);
    }
    for (int i = 0; i < 8; i++) {
        vertex_data.mVtxTexCoordArray[i] =
            JSUConvertOffsetToPtr<void>(i_block, i_block->mpVtxTexCoordArray[i]);
    }

    u32 nrm_size = 12;
    if (getFmtType(vertex_data.mVtxAttrFmtList, GX_VA_NRM) == GX_F32) {
        nrm_size = 12;
    } else {
        nrm_size = 6;
    }

    void* nrm_end = NULL;
    if (vertex_data.mVtxNBTArray != NULL) {
        nrm_end = vertex_data.mVtxNBTArray;
    } else if (vertex_data.mVtxColorArray[0] != NULL) {
        nrm_end = vertex_data.mVtxColorArray[0];
    } else if (vertex_data.mVtxTexCoordArray[0] != NULL) {
        nrm_end = vertex_data.mVtxTexCoordArray[0];
    }

    if (vertex_data.mVtxNrmArray == NULL) {
        vertex_data.mNrmNum = 0;
    } else if (nrm_end != NULL) {
        vertex_data.mNrmNum = ((uintptr_t)nrm_end - (uintptr_t)vertex_data.mVtxNrmArray) / nrm_size + 1;
    } else {
        vertex_data.mNrmNum = (i_block->mBlockSize - (uintptr_t)i_block->mpVtxNrmArray) / nrm_size + 1;
    }

    void* color0_end = NULL;
    if (vertex_data.mVtxColorArray[1] != NULL) {
        color0_end = vertex_data.mVtxColorArray[1];
    } else if (vertex_data.mVtxTexCoordArray[0] != NULL) {
        color0_end = vertex_data.mVtxTexCoordArray[0];
    }

    if (vertex_data.mVtxColorArray[0] == NULL) {
        vertex_data.mColNum = 0;
    } else if (color0_end != NULL) {
        vertex_data.mColNum = ((uintptr_t)color0_end - (uintptr_t)vertex_data.mVtxColorArray[0]) / 4 + 1;
    } else {
        vertex_data.mColNum = (i_block->mBlockSize - (uintptr_t)i_block->mpVtxColorArray[0]) / 4 + 1;
    }

    int local_28 = 0;
    if (vertex_data.mVtxTexCoordArray[1]) {
        color0_end = vertex_data.mVtxTexCoordArray[1];
    }

    if (vertex_data.mVtxTexCoordArray[0] == NULL) {
        vertex_data.mTexCoordNum = 0;
        return;
    }
    if (local_28) {
        vertex_data.mTexCoordNum = (local_28 - (uintptr_t)vertex_data.mVtxTexCoordArray[0]) / 8 + 1;
    } else {
        vertex_data.mTexCoordNum = (i_block->mBlockSize - (uintptr_t)i_block->mpVtxTexCoordArray[0]) / 8 + 1;
    }
}

void J3DModelLoader::readEnvelop(J3DEnvelopeBlock const* i_block) {
    J3D_ASSERT_NULLPTR(724, i_block);
    u16 wevlpMtxNum = i_block->mWEvlpMtxNum;

#ifdef TARGET_PC
    // Empty EVP1 blocks in some TP assets use 0xFFFF as a sentinel while leaving all tables null.
    // Treat that as "no weighted envelopes" so the draw-matrix bookkeeping matches the GC runtime.
    if (wevlpMtxNum == 0xFFFF && i_block->mpWEvlpMixMtxNum == 0 && i_block->mpWEvlpMixIndex == 0 &&
        i_block->mpWEvlpMixWeight == 0 && i_block->mpInvJointMtx == 0)
    {
        wevlpMtxNum = 0;
    }

    if (wevlpMtxNum == 0xFFFF) {
        u32 derivedMtxNum = 0;

        auto nextOffset = [&](u32 off) -> u32 {
            u32 next = i_block->mBlockSize;
            const u32 offsets[] = {
                (u32)i_block->mpWEvlpMixMtxNum,
                (u32)i_block->mpWEvlpMixIndex,
                (u32)i_block->mpWEvlpMixWeight,
                (u32)i_block->mpInvJointMtx,
            };
            for (u32 candidate : offsets) {
                if (candidate > off && candidate < next) {
                    next = candidate;
                }
            }
            return next;
        };

        if (i_block->mpWEvlpMixMtxNum != 0) {
            derivedMtxNum = nextOffset((u32)i_block->mpWEvlpMixMtxNum) - (u32)i_block->mpWEvlpMixMtxNum;
        } else if (i_block->mpInvJointMtx != 0) {
            derivedMtxNum = (i_block->mBlockSize - (u32)i_block->mpInvJointMtx) / (12 * sizeof(f32));
        }
        if (derivedMtxNum > 0 && derivedMtxNum < 0x400) {
            wevlpMtxNum = derivedMtxNum;
        }
    }
#endif

    mpModelData->getJointTree().mWEvlpMtxNum = wevlpMtxNum;
    mpModelData->getJointTree().mWEvlpMixMtxNum =
        JSUConvertOffsetToPtr<u8>(i_block, i_block->mpWEvlpMixMtxNum);
    mpModelData->getJointTree().mWEvlpMixMtxIndex =
        JSUConvertOffsetToPtr<u16>(i_block, i_block->mpWEvlpMixIndex);
    mpModelData->getJointTree().mWEvlpMixWeight =
        JSUConvertOffsetToPtr<f32>(i_block, i_block->mpWEvlpMixWeight);
    mpModelData->getJointTree().mInvJointMtx =
        JSUConvertOffsetToPtr<Mtx>(i_block, i_block->mpInvJointMtx);
#ifdef TARGET_PC
    static int s_envp_log_count = 0;
    if (s_envp_log_count < 8) {
        fprintf(stderr,
                "[J3D] EVP1: block=%p mtxNum=%u mixNumOff=0x%x mixIdxOff=0x%x mixWgtOff=0x%x invMtxOff=0x%x"
                " mixNumPtr=%p mixIdxPtr=%p mixWgtPtr=%p invMtxPtr=%p firstMix=%u\n",
                (const void*)i_block, wevlpMtxNum,
                (u32)i_block->mpWEvlpMixMtxNum, (u32)i_block->mpWEvlpMixIndex,
                (u32)i_block->mpWEvlpMixWeight, (u32)i_block->mpInvJointMtx,
                (void*)mpModelData->getJointTree().mWEvlpMixMtxNum,
                (void*)mpModelData->getJointTree().mWEvlpMixMtxIndex,
                (void*)mpModelData->getJointTree().mWEvlpMixWeight,
                (void*)mpModelData->getJointTree().mInvJointMtx,
                mpModelData->getJointTree().mWEvlpMixMtxNum != NULL
                    ? mpModelData->getJointTree().mWEvlpMixMtxNum[0]
                    : 0);
        fflush(stderr);
        s_envp_log_count++;
    }
#endif
}

void J3DModelLoader::readDraw(J3DDrawBlock const* i_block) {
    J3D_ASSERT_NULLPTR(747, i_block);
    u16 mtxNum = i_block->mMtxNum;
#ifdef TARGET_PC
    if (mtxNum == 0xFFFF && i_block->mpDrawMtxFlag != 0 && i_block->mpDrawMtxIndex > i_block->mpDrawMtxFlag) {
        u32 flagCount = (u32)i_block->mpDrawMtxIndex - (u32)i_block->mpDrawMtxFlag;
        u32 idxCount = (i_block->mBlockSize - i_block->mpDrawMtxIndex) / sizeof(u16);
        u32 derivedMtxNum = flagCount < idxCount ? flagCount : idxCount;
        if (derivedMtxNum > 0 && derivedMtxNum < 0x4000) {
            mtxNum = derivedMtxNum;
        }
    }
#endif
    J3DDrawMtxData* drawMtxData = mpModelData->getDrawMtxData();
    drawMtxData->mEntryNum = mtxNum - mpModelData->getWEvlpMtxNum();
    drawMtxData->mDrawMtxFlag = JSUConvertOffsetToPtr<u8>(i_block, i_block->mpDrawMtxFlag);
    drawMtxData->mDrawMtxIndex = JSUConvertOffsetToPtr<u16>(i_block, i_block->mpDrawMtxIndex);
    u16 i;
    for (i = 0; i < drawMtxData->mEntryNum; i++) {
        if (drawMtxData->mDrawMtxFlag[i] == 1) {
            break;
        }
    }
    drawMtxData->mDrawFullWgtMtxNum = i;
    mpModelData->getJointTree().mWEvlpImportantMtxIdx = new u16[mtxNum];
    J3D_ASSERT_ALLOCMEM(767, mpModelData->getJointTree().mWEvlpImportantMtxIdx);
}

void J3DModelLoader::readJoint(J3DJointBlock const* i_block) {
    J3D_ASSERT_NULLPTR(781, i_block);
    u16 jointNum = i_block->mJointNum;
#ifdef TARGET_PC
    if (jointNum == 0xFFFF && i_block->mpJointInitData != 0 && i_block->mpIndexTable > i_block->mpJointInitData) {
        u32 derivedJointNum = (i_block->mpIndexTable - i_block->mpJointInitData) / 0x40;
        if (derivedJointNum > 0 && derivedJointNum < 0x400) {
            jointNum = derivedJointNum;
        }
    }
#endif
    J3DJointFactory factory(*i_block);
    mpModelData->getJointTree().mJointNum = jointNum;
    if (i_block->mpNameTable != NULL) {
        mpModelData->getJointTree().mJointName =
            new JUTNameTab(JSUConvertOffsetToPtr<ResNTAB>(i_block, i_block->mpNameTable));
        J3D_ASSERT_ALLOCMEM(791, mpModelData->getJointTree().mJointName);
    } else {
        mpModelData->getJointTree().mJointName = NULL;
    }
    mpModelData->getJointTree().mJointNodePointer =
        new J3DJoint*[mpModelData->getJointTree().mJointNum];
    J3D_ASSERT_ALLOCMEM(797, mpModelData->getJointTree().mJointNodePointer);
    for (u16 i = 0; i < mpModelData->getJointNum(); i++) {
        mpModelData->getJointTree().mJointNodePointer[i] = factory.create(i);
    }
}

void J3DModelLoader_v26::readMaterial(J3DMaterialBlock const* i_block, u32 i_flags) {
    J3D_ASSERT_NULLPTR(817, i_block);
    mpMaterialBlock = i_block;
#ifdef TARGET_PC
    J3DMaterialBlock* mutableBlock = const_cast<J3DMaterialBlock*>(i_block);
    u16 hierarchyMaterialNum = 0;
    const J3DModelHierarchy* hierarchyEntry = mpModelData->getHierarchy();
    for (; hierarchyEntry != NULL && hierarchyEntry->mType != 0; hierarchyEntry++) {
        if (hierarchyEntry->mType == 0x11 && hierarchyEntry->mValue < 0x400 &&
            hierarchyEntry->mValue + 1 > hierarchyMaterialNum)
        {
            hierarchyMaterialNum = hierarchyEntry->mValue + 1;
        }
    }
    if (hierarchyMaterialNum > mutableBlock->mMaterialNum && hierarchyMaterialNum < 0x400) {
        fprintf(stderr,
                "[J3D] expanding MAT3 material count from %u to %u based on hierarchy\n",
                mutableBlock->mMaterialNum, hierarchyMaterialNum);
        fflush(stderr);
        mutableBlock->mMaterialNum = hierarchyMaterialNum;
    }
#endif
    J3DMaterialFactory factory(*i_block);
    mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
    mpMaterialTable->mUniqueMatNum = factory.countUniqueMaterials();
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mMaterialName =
            new JUTNameTab(JSUConvertOffsetToPtr<ResNTAB>(i_block, i_block->mpNameTable));
        J3D_ASSERT_ALLOCMEM(832, mpMaterialTable->mMaterialName);
    } else {
        mpMaterialTable->mMaterialName = NULL;
    }
    mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
    J3D_ASSERT_ALLOCMEM(841, mpMaterialTable->mMaterialNodePointer);
    if (i_flags & 0x200000) {
        mpMaterialTable->field_0x10 = new (0x20) J3DMaterial[mpMaterialTable->mUniqueMatNum];
        J3D_ASSERT_ALLOCMEM(846, mpMaterialTable->field_0x10);
    } else {
        mpMaterialTable->field_0x10 = NULL;
    }
    if (i_flags & 0x200000) {
        for (u16 i = 0; i < mpMaterialTable->mUniqueMatNum; i++) {
            factory.create(&mpMaterialTable->field_0x10[i],
                           J3DMaterialFactory::MATERIAL_TYPE_NORMAL, i, i_flags);
            mpMaterialTable->field_0x10[i].mDiffFlag = (uintptr_t)&mpMaterialTable->field_0x10[i] >> 4;
        }
    }
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i] =
            factory.create(NULL, J3DMaterialFactory::MATERIAL_TYPE_NORMAL, i, i_flags);
    }
    if (i_flags & 0x200000) {
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag =
                (uintptr_t)&mpMaterialTable->field_0x10[factory.getMaterialID(i)] >> 4;
            mpMaterialTable->mMaterialNodePointer[i]->mpOrigMaterial =
                &mpMaterialTable->field_0x10[factory.getMaterialID(i)];
        }
    } else {
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag =
                ((uintptr_t)mpMaterialTable->mMaterialNodePointer >> 4) + factory.getMaterialID(i);
        }
    }
}

void J3DModelLoader_v21::readMaterial_v21(J3DMaterialBlock_v21 const* i_block, u32 i_flags) {
    J3D_ASSERT_NULLPTR(913, i_block);
    J3DMaterialFactory_v21 factory(*i_block);
    mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
    mpMaterialTable->mUniqueMatNum = factory.countUniqueMaterials();
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mMaterialName =
            new JUTNameTab(JSUConvertOffsetToPtr<ResNTAB>(i_block, i_block->mpNameTable));
        J3D_ASSERT_ALLOCMEM(930, mpMaterialTable->mMaterialName);
    } else {
        mpMaterialTable->mMaterialName = NULL;
    }
    mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
    J3D_ASSERT_ALLOCMEM(940, mpMaterialTable->mMaterialNodePointer);
    if (i_flags & 0x200000) {
        mpMaterialTable->field_0x10 = new (0x20) J3DMaterial[mpMaterialTable->mUniqueMatNum];
        J3D_ASSERT_ALLOCMEM(945, mpMaterialTable->field_0x10);
    } else {
        mpMaterialTable->field_0x10 = NULL;
    }
    if (i_flags & 0x200000) {
        for (u16 i = 0; i < mpMaterialTable->mUniqueMatNum; i++) {
            factory.create(&mpMaterialTable->field_0x10[i], i, i_flags);
            mpMaterialTable->field_0x10[i].mDiffFlag = (uintptr_t)&mpMaterialTable->field_0x10[i] >> 4;
        }
    }
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i] = factory.create(NULL, i, i_flags);
    }
    if (i_flags & 0x200000) {
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag =
                (uintptr_t)&mpMaterialTable->field_0x10[factory.getMaterialID(i)] >> 4;
            mpMaterialTable->mMaterialNodePointer[i]->mpOrigMaterial =
                &mpMaterialTable->field_0x10[factory.getMaterialID(i)];
        }
    } else {
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = 0xc0000000;
        }
    }
}

void J3DModelLoader::readShape(J3DShapeBlock const* i_block, u32 i_flags) {
    J3D_ASSERT_NULLPTR(1009, i_block);
    mpShapeBlock = i_block;
    J3DShapeTable* shape_table = mpModelData->getShapeTable();
    J3DShapeFactory factory(*i_block);
    u16 shapeNum = i_block->mShapeNum;
#ifdef TARGET_PC
    if (i_block->mpShapeInitData != 0 && i_block->mpIndexTable > i_block->mpShapeInitData) {
        u32 derivedShapeNum = (i_block->mpIndexTable - i_block->mpShapeInitData) / 0x28;
        if (derivedShapeNum > 0 && derivedShapeNum < 0x400 &&
            (shapeNum == 0xFFFF || derivedShapeNum < shapeNum))
        {
            shapeNum = derivedShapeNum;
        }
    }
#endif
#ifdef TARGET_PC
    if (i_block->mBlockSize == 32064) {
        J3DShapeInitData* shapeInitData =
            JSUConvertOffsetToPtr<J3DShapeInitData>(i_block, (uintptr_t)i_block->mpShapeInitData);
        u16* indexTable = JSUConvertOffsetToPtr<u16>(i_block, (uintptr_t)i_block->mpIndexTable);
        fprintf(stderr,
                "[J3D] SHP1 dbg: block=%p rawShapeNum=%u shapeNum=%u shapeInitOff=0x%x idxOff=0x%x"
                " nameOff=0x%x vcdOff=0x%x mtxTblOff=0x%x dlOff=0x%x mtxInitOff=0x%x drawInitOff=0x%x\n",
                (const void*)i_block, i_block->mShapeNum, shapeNum,
                (u32)i_block->mpShapeInitData, (u32)i_block->mpIndexTable, (u32)i_block->mpNameTable,
                (u32)i_block->mpVtxDescList, (u32)i_block->mpMtxTable, (u32)i_block->mpDisplayListData,
                (u32)i_block->mpMtxInitData, (u32)i_block->mpDrawInitData);
        if (shapeInitData != NULL && indexTable != NULL) {
            for (u32 i = 0; i < shapeNum && i < 8; i++) {
                const J3DShapeInitData& init = shapeInitData[indexTable[i]];
                fprintf(stderr,
                        "[J3D] SHP1 dbg: shape[%u] idx=%u type=%u groupNum=%u vtxDescIdx=0x%x"
                        " mtxInitIdx=%u drawInitIdx=%u radius=%f\n",
                        i, indexTable[i], init.mShapeMtxType, init.mMtxGroupNum,
                        init.mVtxDescListIndex, init.mMtxInitDataIndex, init.mDrawInitDataIndex,
                        init.mRadius);
            }
            for (u32 i = 0; i < shapeNum && i < 2; i++) {
                const u8* raw = (const u8*)&shapeInitData[indexTable[i]];
                fprintf(stderr, "[J3D] SHP1 raw[%u]:", i);
                for (u32 j = 0; j < 0x28; j++) {
                    fprintf(stderr, " %02x", raw[j]);
                }
                fprintf(stderr, "\n");
            }
        }
        fflush(stderr);
    }
#endif
    shape_table->mShapeNum = shapeNum;
    if (i_block->mpNameTable != NULL) {
        shape_table->mShapeName =
            new JUTNameTab(JSUConvertOffsetToPtr<ResNTAB>(i_block, i_block->mpNameTable));
        J3D_ASSERT_ALLOCMEM(1026, shape_table->mShapeName);
    } else {
        shape_table->mShapeName = NULL;
    }
    shape_table->mShapeNodePointer = new J3DShape*[shape_table->mShapeNum];
    J3D_ASSERT_ALLOCMEM(1034, shape_table->mShapeNodePointer);
    for (u16 i = 0; i < shape_table->mShapeNum; i++) {
        shape_table->mShapeNodePointer[i] = NULL;
    }
    factory.allocVcdVatCmdBuffer(shape_table->mShapeNum);
    J3DModelHierarchy const* hierarchy_entry = mpModelData->getHierarchy();
    GXVtxDescList* vtx_desc_list = NULL;
#ifdef TARGET_PC
    /* Log hierarchy entries so we can debug shape/material creation failures */
    {
        const J3DModelHierarchy* dbg = hierarchy_entry;
        int shape_hits = 0;
        for (int idx = 0; dbg->mType != 0 && idx < 200; dbg++, idx++) {
            if (dbg->mType == 0x12) shape_hits++;
        }
        if (shape_hits < (int)shape_table->mShapeNum) {
            fprintf(stderr, "[J3D] readShape: hierarchy has %d shape entries for %d shapes — dumping:\n",
                    shape_hits, shape_table->mShapeNum);
            const J3DModelHierarchy* dbg2 = hierarchy_entry;
            for (int idx = 0; idx < 200 && (dbg2->mType != 0 || idx == 0); dbg2++, idx++) {
                fprintf(stderr, "  hier[%d]: type=0x%04x value=%u\n", idx, dbg2->mType, dbg2->mValue);
                if (dbg2->mType == 0) break;
            }
        }
    }
#endif
    for (; hierarchy_entry->mType != 0; hierarchy_entry++) {
        if (hierarchy_entry->mType == 0x12 && hierarchy_entry->mValue < shape_table->mShapeNum) {
            shape_table->mShapeNodePointer[hierarchy_entry->mValue] =
                factory.create(hierarchy_entry->mValue, i_flags, vtx_desc_list);
            vtx_desc_list = factory.getVtxDescList(hierarchy_entry->mValue);
        }
    }

#ifdef TARGET_PC
    for (u16 i = 0; i < shape_table->mShapeNum; i++) {
        if (shape_table->mShapeNodePointer[i] == NULL) {
            shape_table->mShapeNodePointer[i] = factory.create(i, i_flags, vtx_desc_list);
            vtx_desc_list = factory.getVtxDescList(i);
        }
    }
#endif
}

void J3DModelLoader::readTexture(J3DTextureBlock const* i_block) {
    J3D_ASSERT_NULLPTR(1067, i_block);
    u16 texture_num = i_block->mTextureNum;
    ResTIMG* texture_res = JSUConvertOffsetToPtr<ResTIMG>(i_block, i_block->mpTextureRes);
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mTextureName =
            new JUTNameTab(JSUConvertOffsetToPtr<ResNTAB>(i_block, i_block->mpNameTable));
        J3D_ASSERT_ALLOCMEM(1077, mpMaterialTable->mTextureName);
    } else {
        mpMaterialTable->mTextureName = NULL;
    }
    mpMaterialTable->mTexture = new J3DTexture(texture_num, texture_res);
    J3D_ASSERT_ALLOCMEM(1084, mpMaterialTable->mTexture);
}

void J3DModelLoader_v26::readMaterialTable(J3DMaterialBlock const* i_block, u32 i_flags) {
    J3D_ASSERT_NULLPTR(1101, i_block);
    J3DMaterialFactory factory(*i_block);
    mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mMaterialName =
            new JUTNameTab(JSUConvertOffsetToPtr<ResNTAB>(i_block, i_block->mpNameTable));
        J3D_ASSERT_ALLOCMEM(1114, mpMaterialTable->mMaterialName);
    } else {
        mpMaterialTable->mMaterialName = NULL;
    }
    mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
    J3D_ASSERT_ALLOCMEM(1121, mpMaterialTable->mMaterialNodePointer);
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i] =
            factory.create(NULL, J3DMaterialFactory::MATERIAL_TYPE_NORMAL, i, i_flags);
    }
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag =
            (uintptr_t)mpMaterialTable->mMaterialNodePointer + factory.getMaterialID(i);
    }
}

void J3DModelLoader_v21::readMaterialTable_v21(J3DMaterialBlock_v21 const* i_block, u32 i_flags) {
    J3D_ASSERT_NULLPTR(1152, i_block);
    J3DMaterialFactory_v21 factory(*i_block);
    mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mMaterialName =
            new JUTNameTab(JSUConvertOffsetToPtr<ResNTAB>(i_block, i_block->mpNameTable));
        J3D_ASSERT_ALLOCMEM(1165, mpMaterialTable->mMaterialName);
    } else {
        mpMaterialTable->mMaterialName = NULL;
    }
    mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
    J3D_ASSERT_ALLOCMEM(1172, mpMaterialTable->mMaterialNodePointer);
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i] =
            factory.create(NULL, i, i_flags);
    }
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag =
            ((uintptr_t)mpMaterialTable->mMaterialNodePointer >> 4) + factory.getMaterialID(i);
    }
}

void J3DModelLoader::readTextureTable(J3DTextureBlock const* i_block) {
    J3D_ASSERT_NULLPTR(1200, i_block);
    u16 texture_num = i_block->mTextureNum;
    ResTIMG* texture_res = JSUConvertOffsetToPtr<ResTIMG>(i_block, i_block->mpTextureRes);
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mTextureName =
            new JUTNameTab(JSUConvertOffsetToPtr<ResNTAB>(i_block, i_block->mpNameTable));
        J3D_ASSERT_ALLOCMEM(1211, mpMaterialTable->mTextureName);
    } else {
        mpMaterialTable->mTextureName = NULL;
    }
    mpMaterialTable->mTexture = new J3DTexture(texture_num, texture_res);
    J3D_ASSERT_ALLOCMEM(1218, mpMaterialTable->mTexture);
}

void J3DModelLoader::readPatchedMaterial(J3DMaterialBlock const* i_block, u32 i_flags) {
    J3D_ASSERT_NULLPTR(1234, i_block);
    J3DMaterialFactory factory(*i_block);
    mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
    mpMaterialTable->mUniqueMatNum = factory.countUniqueMaterials();
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mMaterialName =
            new JUTNameTab(JSUConvertOffsetToPtr<ResNTAB>(i_block, i_block->mpNameTable));
        J3D_ASSERT_ALLOCMEM(1251, mpMaterialTable->mMaterialName);
    } else {
        mpMaterialTable->mMaterialName = NULL;
    }
    mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
    J3D_ASSERT_ALLOCMEM(1260, mpMaterialTable->mMaterialNodePointer);
    mpMaterialTable->field_0x10 = NULL;
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i] =
            factory.create(NULL, J3DMaterialFactory::MATERIAL_TYPE_PATCHED, i, i_flags);
        mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag =
            ((uintptr_t)mpMaterialTable->mMaterialNodePointer >> 4) + factory.getMaterialID(i);
    }
}

void J3DModelLoader::readMaterialDL(J3DMaterialDLBlock const* i_block, u32 i_flags) {
    J3D_ASSERT_NULLPTR(1290, i_block);
    J3DMaterialFactory factory(*i_block);
    s32 flags;
    if (mpMaterialTable->mMaterialNum == 0) {
        mpMaterialTable->field_0x1c = 1;
        mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
        mpMaterialTable->mUniqueMatNum = i_block->mMaterialNum;
        if (i_block->mpNameTable != NULL) {
            mpMaterialTable->mMaterialName =
                new JUTNameTab(JSUConvertOffsetToPtr<ResNTAB>(i_block, i_block->mpNameTable));
            J3D_ASSERT_ALLOCMEM(1312, mpMaterialTable->mMaterialName);
        } else {
            mpMaterialTable->mMaterialName = NULL;
        }
        mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
        J3D_ASSERT_ALLOCMEM(1320, mpMaterialTable->mMaterialNodePointer);
        mpMaterialTable->field_0x10 = NULL;
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            flags = i_flags;
            mpMaterialTable->mMaterialNodePointer[i] =
                factory.create(NULL, J3DMaterialFactory::MATERIAL_TYPE_LOCKED, i, flags);
        }
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = 0xc0000000;
        }
    } else {
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            flags = i_flags;
            mpMaterialTable->mMaterialNodePointer[i] =
                factory.create(mpMaterialTable->mMaterialNodePointer[i],
                               J3DMaterialFactory::MATERIAL_TYPE_LOCKED, i, flags);
        }
    }
}

void J3DModelLoader::modifyMaterial(u32 i_flags) {
    if (i_flags & 0x2000) {
        J3DMaterialFactory factory(*mpMaterialBlock);
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            factory.modifyPatchedCurrentMtx(mpMaterialTable->mMaterialNodePointer[i], i);
        }
    }
}
