#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/J3DGraphAnimator/J3DShapeTable.h"
#include "JSystem/J3DGraphBase/J3DShape.h"

void J3DShapeTable::hide() {
    u16 shapeNum = mShapeNum;
    for (u16 i = 0; i < shapeNum; i++) {
#ifdef TARGET_PC
        if (mShapeNodePointer[i] == NULL) continue;
#endif
        mShapeNodePointer[i]->onFlag(1);
    }
}

void J3DShapeTable::show() {
    u16 shapeNum = mShapeNum;
    for (u16 i = 0; i < shapeNum; i++) {
#ifdef TARGET_PC
        if (mShapeNodePointer[i] == NULL) continue;
#endif
        mShapeNodePointer[i]->offFlag(1);
    }
}

void J3DShapeTable::initShapeNodes(J3DDrawMtxData* pMtxData, J3DVertexData* pVtxData) {
#ifdef TARGET_PC
    /* On PC, skip VcdVatCmd generation entirely — the GX display list
     * system (GD) writes hardware register commands that have no meaning
     * on the OpenGL backend. The VcdVatCmd data is only used by the GC
     * hardware FIFO. All vertex format setup on PC goes through the GX
     * API functions (GXSetVtxDesc, etc.) which our pc_gx.cpp handles. */
    u16 shapeNum = mShapeNum;
    for (u16 i = 0; i < shapeNum; i++) {
        J3DShape* shapeNode = mShapeNodePointer[i];
        if (shapeNode == NULL) continue;
        shapeNode->setDrawMtxDataPointer(pMtxData);
        shapeNode->setVertexDataPointer(pVtxData);
    }
#else
    u16 shapeNum = mShapeNum;
    for (u16 i = 0; i < shapeNum; i++) {
        J3DShape* shapeNode = mShapeNodePointer[i];
        shapeNode->setDrawMtxDataPointer(pMtxData);
        shapeNode->setVertexDataPointer(pVtxData);
        shapeNode->makeVcdVatCmd();
    }
#endif
}

void J3DShapeTable::sortVcdVatCmd() {
    u16 shapeNum = mShapeNum;
    for (u16 next = 0; next < shapeNum; next++) {
#ifdef TARGET_PC
        if (mShapeNodePointer[next] == NULL) continue;
#endif
        for (u16 prev = 0; prev < next; prev++) {
#ifdef TARGET_PC
            if (mShapeNodePointer[prev] == NULL) continue;
#endif
            if (mShapeNodePointer[next]->isSameVcdVatCmd(mShapeNodePointer[prev])) {
                mShapeNodePointer[next]->setVcdVatCmd(mShapeNodePointer[prev]->getVcdVatCmd());
            }
        }
    }
}
