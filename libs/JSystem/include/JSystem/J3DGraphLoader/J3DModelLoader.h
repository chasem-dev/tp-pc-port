#ifndef J3DMODELLOADER_H
#define J3DMODELLOADER_H

#include "JSystem/J3DGraphBase/J3DSys.h"
#include <mtx.h>

class J3DModelData;
class J3DMaterialTable;
struct J3DModelHierarchy;

/**
 * @ingroup jsystem-j3d
 * 
 */
struct J3DModelBlock {
    /* 0x00 */ u32 mBlockType;
    /* 0x04 */ u32 mBlockSize;
};

/* On 64-bit, void* is 8 bytes but J3D binary offsets are 4 bytes.
 * Use J3D_PTR for struct fields that hold offsets in the binary data.
 * These get passed to JSUConvertOffsetToPtr which masks to 32 bits. */
#ifdef TARGET_PC
#define J3D_PTR u32
#else
#define J3D_PTR void*
#endif

/**
 * @ingroup jsystem-j3d
 * 
 */
struct J3DModelFileData {
    /* 0x00 */ u32 mMagic1;
    /* 0x04 */ u32 mMagic2;
    /* 0x08 */ u8 field_0x08[4];
    /* 0x0C */ u32 mBlockNum;
    /* 0x10 */ u8 field_0x10[0x1C - 0x10];
    /* 0x1C */ int field_0x1c;
    /* 0x20 */ J3DModelBlock mBlocks[1];
};

/**
 * @ingroup jsystem-j3d
 * 
 */
struct J3DModelInfoBlock : public J3DModelBlock {
    /* 0x08 */ u16 mFlags;
    /* 0x0C */ u32 mPacketNum;
    /* 0x10 */ u32 mVtxNum;
    /**/ J3D_PTR mpHierarchy;
}; // size 0x18

/**
 * @ingroup jsystem-j3d
 * 
 */
struct J3DVertexBlock : public J3DModelBlock {
    /**/ J3D_PTR mpVtxAttrFmtList;
    /**/ J3D_PTR mpVtxPosArray;
    /**/ J3D_PTR mpVtxNrmArray;
    /**/ J3D_PTR mpVtxNBTArray;
    /**/ J3D_PTR mpVtxColorArray[2];
    /**/ J3D_PTR mpVtxTexCoordArray[8];
}; // size 0x40

/**
 * @ingroup jsystem-j3d
 * 
 */
struct J3DEnvelopeBlock : public J3DModelBlock {
    /* 0x08 */ u16 mWEvlpMtxNum;
    /**/ J3D_PTR mpWEvlpMixMtxNum;
    /**/ J3D_PTR mpWEvlpMixIndex;
    /**/ J3D_PTR mpWEvlpMixWeight;
    /**/ J3D_PTR mpInvJointMtx;
}; // size 0x1C

/**
 * @ingroup jsystem-j3d
 * 
 */
struct J3DDrawBlock : public J3DModelBlock {
    /* 0x08 */ u16 mMtxNum;
    /**/ J3D_PTR mpDrawMtxFlag;
    /**/ J3D_PTR mpDrawMtxIndex;
}; // size 0x14

/**
 * @ingroup jsystem-j3d
 * 
 */
struct J3DJointBlock : public J3DModelBlock {
    /* 0x08 */ u16 mJointNum;
    /**/ J3D_PTR mpJointInitData;
    /**/ J3D_PTR mpIndexTable;
    /**/ J3D_PTR mpNameTable;
}; // size 0x18

/**
 * @ingroup jsystem-j3d
 * 
 */
struct J3DMaterialBlock : public J3DModelBlock {
    /* 0x08 */ u16 mMaterialNum;
    /**/ J3D_PTR mpMaterialInitData;
    /**/ J3D_PTR mpMaterialID;
    /**/ J3D_PTR mpNameTable;
    /**/ J3D_PTR mpIndInitData;
    /**/ J3D_PTR mpCullMode;
    /**/ J3D_PTR mpMatColor;
    /**/ J3D_PTR mpColorChanNum;
    /**/ J3D_PTR mpColorChanInfo;
    /**/ J3D_PTR mpAmbColor;
    /**/ J3D_PTR mpLightInfo;
    /**/ J3D_PTR mpTexGenNum;
    /**/ J3D_PTR mpTexCoordInfo;
    /**/ J3D_PTR mpTexCoord2Info;
    /**/ J3D_PTR mpTexMtxInfo;
    /* 0x44 */ J3D_PTR field_0x44;
    /**/ J3D_PTR mpTexNo;
    /**/ J3D_PTR mpTevOrderInfo;
    /**/ J3D_PTR mpTevColor;
    /**/ J3D_PTR mpTevKColor;
    /**/ J3D_PTR mpTevStageNum;
    /**/ J3D_PTR mpTevStageInfo;
    /**/ J3D_PTR mpTevSwapModeInfo;
    /**/ J3D_PTR mpTevSwapModeTableInfo;
    /**/ J3D_PTR mpFogInfo;
    /**/ J3D_PTR mpAlphaCompInfo;
    /**/ J3D_PTR mpBlendInfo;
    /**/ J3D_PTR mpZModeInfo;
    /**/ J3D_PTR mpZCompLoc;
    /**/ J3D_PTR mpDither;
    /**/ J3D_PTR mpNBTScaleInfo;
};

/**
 * @ingroup jsystem-j3d
 * 
 */
struct J3DMaterialBlock_v21 : public J3DModelBlock {
    /* 0x08 */ u16 mMaterialNum;
    /**/ J3D_PTR mpMaterialInitData;
    /**/ J3D_PTR mpMaterialID;
    /**/ J3D_PTR mpNameTable;
    /**/ J3D_PTR mpCullMode;
    /**/ J3D_PTR mpMatColor;
    /**/ J3D_PTR mpColorChanNum;
    /**/ J3D_PTR mpColorChanInfo;
    /**/ J3D_PTR mpTexGenNum;
    /**/ J3D_PTR mpTexCoordInfo;
    /**/ J3D_PTR mpTexCoord2Info;
    /**/ J3D_PTR mpTexMtxInfo;
    /* 0x38 */ J3D_PTR field_0x38;
    /**/ J3D_PTR mpTexNo;
    /**/ J3D_PTR mpTevOrderInfo;
    /**/ J3D_PTR mpTevColor;
    /**/ J3D_PTR mpTevKColor;
    /**/ J3D_PTR mpTevStageNum;
    /**/ J3D_PTR mpTevStageInfo;
    /**/ J3D_PTR mpTevSwapModeInfo;
    /**/ J3D_PTR mpTevSwapModeTableInfo;
    /**/ J3D_PTR mpFogInfo;
    /**/ J3D_PTR mpAlphaCompInfo;
    /**/ J3D_PTR mpBlendInfo;
    /**/ J3D_PTR mpZModeInfo;
    /**/ J3D_PTR mpZCompLoc;
    /**/ J3D_PTR mpDither;
    /**/ J3D_PTR mpNBTScaleInfo;
};

/**
 * @ingroup jsystem-j3d
 * 
 */
struct J3DMaterialDLBlock : public J3DModelBlock {
    /* 0x08 */ u16 mMaterialNum;
    /**/ J3D_PTR mpDisplayListInit;
    /**/ J3D_PTR mpPatchingInfo;
    /**/ J3D_PTR mpCurrentMtxInfo;
    /**/ J3D_PTR mpMaterialMode;
    /* 0x1C */ J3D_PTR field_0x1c;
    /**/ J3D_PTR mpNameTable;
};

/**
 * @ingroup jsystem-j3d
 * 
 */
struct J3DShapeBlock : public J3DModelBlock {
    /* 0x08 */ u16 mShapeNum;
    /**/ J3D_PTR mpShapeInitData;
    /**/ J3D_PTR mpIndexTable;
    /**/ J3D_PTR mpNameTable;
    /**/ J3D_PTR mpVtxDescList;
    /**/ J3D_PTR mpMtxTable;
    /**/ J3D_PTR mpDisplayListData;
    /**/ J3D_PTR mpMtxInitData;
    /**/ J3D_PTR mpDrawInitData;
}; // size 0x2C

/**
 * @ingroup jsystem-j3d
 * 
 */
struct J3DTextureBlock : public J3DModelBlock {
    /* 0x08 */ u16 mTextureNum;
    /**/ J3D_PTR mpTextureRes;
    /**/ J3D_PTR mpNameTable;
};

enum J3DModelLoaderFlagTypes {
    J3DMLF_None = 0x00000000,

    J3DMLF_MtxSoftImageCalc = 0x00000001,
    J3DMLF_MtxMayaCalc = 0x00000002,
    J3DMLF_MtxBasicCalc = 0x00000004,
    J3DMLF_04 = 0x00000008,
    J3DMLF_MtxTypeMask = J3DMLF_MtxSoftImageCalc | J3DMLF_MtxMayaCalc | J3DMLF_MtxBasicCalc |
                         J3DMLF_04,  // 0 - 2 (0 = Basic, 1 = SoftImage, 2 = Maya)

    J3DMLF_UseImmediateMtx = 0x00000010,
    J3DMLF_UsePostTexMtx = 0x00000020,
    J3DMLF_07 = 0x00000040,
    J3DMLF_08 = 0x00000080,
    J3DMLF_NoMatrixTransform = 0x00000100,
    J3DMLF_10 = 0x00000200,
    J3DMLF_11 = 0x00000400,
    J3DMLF_12 = 0x00000800,
    J3DMLF_13 = 0x00001000,
    J3DMLF_DoBdlMaterialCalc = 0x00002000,
    J3DMLF_15 = 0x00004000,
    J3DMLF_16 = 0x00008000,
    J3DMLF_TevNumShift = 0x00010000,
    J3DMLF_18 = 0x00020000,
    J3DMLF_UseSingleSharedDL = 0x00040000,
    J3DMLF_20 = 0x00080000,
    J3DMLF_21 = 0x00100000,
    J3DMLF_UseUniqueMaterials = 0x00200000,
    J3DMLF_23 = 0x00400000,
    J3DMLF_24 = 0x00800000,
    J3DMLF_Material_UseIndirect = 0x01000000,
    J3DMLF_26 = 0x02000000,
    J3DMLF_27 = 0x04000000,
    J3DMLF_Material_TexGen_Block4 = 0x08000000,
    J3DMLF_Material_PE_Full = 0x10000000,
    J3DMLF_Material_PE_FogOff = 0x20000000,
    J3DMLF_Material_Color_LightOn = 0x40000000,
    J3DMLF_Material_Color_AmbientOn = 0x80000000
};

static inline u32 getMdlDataFlag_TevStageNum(u32 flags) { return (flags & 0x001f0000) >> 0x10; }
static inline u32 getMdlDataFlag_TexGenFlag(u32 flags) { return flags & 0x0c000000; }
static inline u32 getMdlDataFlag_ColorFlag(u32 flags) { return flags & 0xc0000000; }
static inline u32 getMdlDataFlag_PEFlag(u32 flags) { return flags & 0x30000000; }
static inline u32 getMdlDataFlag_MtxLoadType(u32 flags) { return flags & 0x10; }

/**
 * @ingroup jsystem-j3d
 * 
 */
class J3DModelLoader {
public:
    J3DModelLoader();
    void readInformation(J3DModelInfoBlock const*, u32);
    void readVertex(J3DVertexBlock const*);
    void readEnvelop(J3DEnvelopeBlock const*);
    void readDraw(J3DDrawBlock const*);
    void readJoint(J3DJointBlock const*);
    void readShape(J3DShapeBlock const*, u32);
    void readTexture(J3DTextureBlock const*);
    void readTextureTable(J3DTextureBlock const*);
    void readPatchedMaterial(J3DMaterialBlock const*, u32);
    void readMaterialDL(J3DMaterialDLBlock const*, u32);
    void modifyMaterial(u32);

    u32 calcSizeInformation(J3DModelInfoBlock const*, u32);
    u32 calcSizeJoint(J3DJointBlock const*);
    u32 calcSizeEnvelope(J3DEnvelopeBlock const*);
    u32 calcSizeDraw(J3DDrawBlock const*);
    u32 calcSizeShape(J3DShapeBlock const*, u32);
    u32 calcSizeTexture(J3DTextureBlock const*);
    u32 calcSizeTextureTable(J3DTextureBlock const*);
    u32 calcSizePatchedMaterial(J3DMaterialBlock const*, u32);
    u32 calcSizeMaterialDL(J3DMaterialDLBlock const*, u32);


    virtual J3DModelData* load(void const*, u32);
    virtual J3DMaterialTable* loadMaterialTable(void const*);
    virtual J3DModelData* loadBinaryDisplayList(void const*, u32);
    virtual u32 calcLoadSize(void const*, u32);
    virtual u32 calcLoadMaterialTableSize(void const*);
    virtual u32 calcLoadBinaryDisplayListSize(void const*, u32);
    virtual u16 countMaterialNum(void const*);
    virtual void setupBBoardInfo();
    virtual ~J3DModelLoader() {}
    virtual void readMaterial(J3DMaterialBlock const*, u32) {}
    virtual void readMaterial_v21(J3DMaterialBlock_v21 const*, u32) {}
    virtual void readMaterialTable(J3DMaterialBlock const*, u32) {}
    virtual void readMaterialTable_v21(J3DMaterialBlock_v21 const*, u32) {}
    virtual u32 calcSizeMaterial(J3DMaterialBlock const*, u32) { return false; }
    virtual u32 calcSizeMaterialTable(J3DMaterialBlock const*, u32) { return false; }

    /* 0x04 */ J3DModelData* mpModelData;
    /* 0x08 */ J3DMaterialTable* mpMaterialTable;
    /* 0x0C */ J3DShapeBlock const* mpShapeBlock;
    /* 0x10 */ J3DMaterialBlock const* mpMaterialBlock;
    /* 0x14 */ J3DModelHierarchy* mpModelHierarchy;
    /* 0x18 */ u8 field_0x18;
    /* 0x19 */ u8 field_0x19;
    /* 0x1A */ u16 mEnvelopeSize;
};

/**
 * @ingroup jsystem-j3d
 * 
 */
class J3DModelLoader_v26 : public J3DModelLoader {
public:
    ~J3DModelLoader_v26() {}
    void readMaterial(J3DMaterialBlock const*, u32);
    void readMaterialTable(J3DMaterialBlock const*, u32);
    u32 calcSizeMaterial(J3DMaterialBlock const*, u32);
    u32 calcSizeMaterialTable(J3DMaterialBlock const*, u32);
};

/**
 * @ingroup jsystem-j3d
 *
 */
class J3DModelLoader_v21 : public J3DModelLoader {
public:
    ~J3DModelLoader_v21() {}
    void readMaterial_v21(J3DMaterialBlock_v21 const*, u32);
    void readMaterialTable_v21(J3DMaterialBlock_v21 const*, u32);
};

/**
 * @ingroup jsystem-j3d
 * 
 */
class J3DModelLoaderDataBase {
public:
    static J3DModelData* load(void const* i_data, u32 i_flags, u32 i_dataSizeLimit = 0);
    static J3DModelData* loadBinaryDisplayList(const void* i_data, u32 flags, u32 i_dataSizeLimit = 0);
};

/**
 * @ingroup jsystem-j3d
 *
 */
struct J3DMtxCalcJ3DSysInitSoftimage {
    static void init(const Vec& param_0, const Mtx& param_1) {
        J3DSys::mCurrentS = param_0;
        MTXCopy(param_1, J3DSys::mCurrentMtx);
    }
};

#endif /* J3DMODELLOADER_H */
