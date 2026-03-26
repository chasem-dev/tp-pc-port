/**
 * JUTFader.cpp
 * JUtility - Color Fader
 */

#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/JUtility/JUTFader.h"
#include "JSystem/J2DGraph/J2DOrthoGraph.h"

JUTFader::JUTFader(int x, int y, int width, int height, JUtility::TColor pColor)
    : mColor(pColor), mBox(x, y, x + width, y + height) {
    mStatus = 0;
    field_0x8 = 0;
    field_0xa = 0;
    field_0x24 = 0;
    mEStatus = UNKSTATUS_M1;
}

void JUTFader::control() {
    if (0 <= mEStatus && mEStatus-- == 0) {
		mStatus = field_0x24;
	}

	if (mStatus == 1) {
		return;
	}
    
	switch (mStatus) {
    case 0:
        mColor.a = 0xFF;
        break;
    case 2:
        mColor.a = 0xFF - ((++field_0xa * 0xFF) / field_0x8);

        if (field_0xa >= field_0x8) {
            mStatus = 1;
        }

        break;
    case 3:
        mColor.a = ((++field_0xa * 0xFF) / field_0x8);

        if (field_0xa >= field_0x8) {
            mStatus = 0;
        }

        break;
	}
	draw();
}

void JUTFader::draw() {
#ifdef TARGET_PC
    /* Debug: force fader transparent so we can see the 3D scene */
    mColor.a = 0;
    return;
#endif
    if (mColor.a != 0) {
#ifdef TARGET_PC
        /* On PC, set up complete GX state for the fade overlay quad.
         * The J2DOrthoGraph::fillBox path relies on setup2D state which
         * has alpha compare GX_GREATER(0) and no vertex color descriptor,
         * preventing the overlay from rendering over existing content. */
        GXSetNumChans(1);
        GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG,
                       GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        GXSetChanMatColor(GX_COLOR0A0, *(GXColor*)&mColor);
        GXSetNumTexGens(0);
        GXSetNumTevStages(1);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
        GXSetZMode(GX_DISABLE, GX_ALWAYS, GX_DISABLE);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_SET);
        GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
        GXSetCullMode(GX_CULL_NONE);
        GXSetNumIndStages(0);

        Mtx44 mtx;
        C_MTXOrtho(mtx, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 10.0f);
        GXSetProjection(mtx, GX_ORTHOGRAPHIC);
        Mtx posMtx;
        MTXIdentity(posMtx);
        GXLoadPosMtxImm(posMtx, GX_PNMTX0);
        GXSetCurrentMtx(0);
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_CLR_RGBA, GX_S8, 0);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3s8(0, 0, -5);
        GXPosition3s8(1, 0, -5);
        GXPosition3s8(1, 1, -5);
        GXPosition3s8(0, 1, -5);
        GXEnd();
#else
        J2DOrthoGraph orthograph;
        orthograph.setColor(mColor);
        orthograph.fillBox(mBox);
#endif
    }
}

bool JUTFader::startFadeIn(int param_0) {
    bool statusCheck = mStatus == 0;

#ifdef TARGET_PC
    /* On PC, the draw function may call startFadeIn before control() finishes
     * the previous fade-out (status 3). Force completion so the new fade can start. */
    if (!statusCheck && mStatus == 3) {
        mStatus = 0;
        mColor.a = 0xFF;
        statusCheck = true;
    }
#endif

    if (statusCheck) {
        mStatus = 2;
        field_0xa = 0;
        field_0x8 = param_0;
    }

    return statusCheck;
}

bool JUTFader::startFadeOut(int param_0) {
    bool statusCheck = mStatus == 1;

#ifdef TARGET_PC
    /* Same race fix for startFadeOut: if a fade-in is nearly complete, force it. */
    if (!statusCheck && mStatus == 2) {
        mStatus = 1;
        mColor.a = 0;
        statusCheck = true;
    }
#endif

    if (statusCheck) {
        mStatus = 3;
        field_0xa = 0;
        field_0x8 = param_0;
    }

    return statusCheck;
}

void JUTFader::setStatus(JUTFader::EStatus i_status, int param_1) {
    switch (i_status) {
    case 0: 
        if (param_1 != 0) {
            field_0x24 = 0;
            mEStatus = param_1;
            break;
        }

        mStatus = 0;
        field_0x24 = 0;
        mEStatus = 0;
        break;
    case 1: 
        if (param_1 != 0) {
            field_0x24 = 1;
            mEStatus = param_1;
            break;
        }

        mStatus = 1;
        field_0x24 = 1;
        mEStatus = 0;
        break;
    }
}

JUTFader::~JUTFader() {}
