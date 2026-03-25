//
// J3DModelData
//

#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphAnimator/J3DMaterialAnm.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphBase/J3DMaterial.h"

void J3DModelData::clear() {
    mpRawData = 0;
    mFlags = 0;
    mbHasBumpArray = 0;
    mbHasBillboard = 0;
}

J3DModelData::J3DModelData() {
    clear();
}

s32 J3DModelData::newSharedDisplayList(u32 mdlFlags) {
    u16 matNum = getMaterialNum();

    for (u16 i = 0; i < matNum; i++) {
        J3DMaterial* mat = getMaterialNodePointer(i);
#ifdef TARGET_PC
        if (mat == NULL) {
            fprintf(stderr, "[J3D] WARNING: material %d/%d is NULL — skipping DL alloc\n", i, matNum);
            continue;
        }
#endif
        s32 ret;
        u32 dlSize = 0;
#ifdef TARGET_PC
        /* Corrupt MAT3 entries can surface as mode=0 during early boot assets.
         * Skip shared-DL generation for those materials to avoid hard faults in
         * countDLSize()/makeSharedDisplayList; they will use live material load. */
        if (mat->getMaterialMode() == 0) {
            continue;
        }
#endif
        dlSize = mat->countDLSize();
        if (mdlFlags & J3DMdlFlag_UseSingleDL) {
            ret = mat->newSingleSharedDisplayList(dlSize);
            if (ret != kJ3DError_Success)
                return ret;
        } else {
            ret = mat->newSharedDisplayList(dlSize);
            if (ret != kJ3DError_Success)
                return ret;
        }
    }

    return kJ3DError_Success;
}

void J3DModelData::indexToPtr() {
    j3dSys.setTexture(getTexture());

    static BOOL sInterruptFlag = OSDisableInterrupts();
    OSDisableScheduler();

    GDLObj gdl_obj;
    u16 matNum = getMaterialNum();
    for (u16 i = 0; i < matNum; i++) {
        J3DMaterial* matNode = getMaterialNodePointer(i);
#ifdef TARGET_PC
        if (matNode == NULL) {
            fprintf(stderr, "[J3D] WARNING: indexToPtr material %d/%d is NULL\n", i, matNum);
            continue;
        }
#endif
        J3DDisplayListObj* dl_obj = matNode->getSharedDisplayListObj();
#ifdef TARGET_PC
        if (dl_obj == NULL || dl_obj->getDisplayList(0) == NULL) {
            fprintf(stderr, "[J3D] WARNING: indexToPtr material %d DL is NULL\n", i);
            continue;
        }
#endif
        GDInitGDLObj(&gdl_obj, dl_obj->getDisplayList(0), dl_obj->getDisplayListSize());
        GDSetCurrent(&gdl_obj);
        matNode->getTevBlock()->indexToPtr();
    }

    GDSetCurrent(NULL);
    OSEnableScheduler();
    OSRestoreInterrupts(sInterruptFlag);
}

void J3DModelData::makeSharedDL() {
    j3dSys.setTexture(getTexture());

    u16 matNum = getMaterialNum();
    for (u16 i = 0; i < matNum; i++) {
        J3DMaterial* mat = getMaterialNodePointer(i);
#ifdef TARGET_PC
        if (mat == NULL) {
            fprintf(stderr, "[J3D] WARNING: makeSharedDL material %d/%d is NULL\n", i, matNum);
            continue;
        }
        if (mat->getSharedDisplayListObj() == NULL) {
            continue;
        }
#endif
        mat->makeSharedDisplayList();
    }
}

void J3DModelData::simpleCalcMaterial(u16 idx, Mtx param_1) {
    syncJ3DSysFlags();

    J3DMaterial* mat;
    J3DJoint* jointNode = NULL;
#ifdef TARGET_PC
    if (idx == 0) {
        jointNode = mJointTree.getRootNode();
    }
#endif
    if (jointNode == NULL) {
        jointNode = getJointNodePointer(idx);
    }
    if (jointNode == NULL) {
        return;
    }
    for (mat = jointNode->getMesh(); mat != NULL; mat = mat->getNext()) {
        if (mat->getMaterialAnm() != NULL) {
            mat->getMaterialAnm()->calc(mat);
        }
        mat->calc(param_1);
    }
}

void J3DModelData::syncJ3DSysPointers() const {
    j3dSys.setTexture(getTexture());
    j3dSys.setVtxPos(getVtxPosArray());
    j3dSys.setVtxNrm(getVtxNrmArray());
    j3dSys.setVtxCol(getVtxColorArray(0));
}

void J3DModelData::syncJ3DSysFlags() const {
    if (checkFlag(0x20)) {
        j3dSys.onFlag(J3DSysFlag_PostTexMtx);
    } else {
        j3dSys.offFlag(J3DSysFlag_PostTexMtx);
    }
}
