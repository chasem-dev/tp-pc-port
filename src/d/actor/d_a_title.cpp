#include "d/dolzel_rel.h" // IWYU pragma: keep

#include "d/actor/d_a_title.h"
#include "d/d_s_logo.h"
#include "d/d_s_play.h"
#include "d/d_demo.h"
#include "d/d_pane_class_alpha.h"
#include "d/d_menu_collect.h"
#include "m_Do/m_Do_Reset.h"
#include "m_Do/m_Do_controller_pad.h"
#include "d/d_com_inf_game.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "f_op/f_op_overlap_mng.h"
#include "f_op/f_op_msg_mng.h"
#include "f_op/f_op_scene_mng.h"
#include "JSystem/J2DGraph/J2DScreen.h"
#include "JSystem/JKernel/JKRMemArchive.h"
#include "JSystem/J2DGraph/J2DTextBox.h"
#include "m_Do/m_Do_graphic.h"

class daTit_HIO_c : public JORReflexible {
public:
    daTit_HIO_c();

    virtual ~daTit_HIO_c() {}
    void genMessage(JORMContext*);

    /* 0x04 */ s8 id;
    /* 0x08 */ f32 mPSScaleX;
    /* 0x0C */ f32 mPSScaleY;
    /* 0x10 */ f32 mPSPosX;
    /* 0x14 */ f32 mPSPosY;
    #if DEBUG
    /* 0x18 */ u8 unk_0x18[0x48 - 0x18];
    #endif
    /* 0x18 */ u8 mAppear;
    /* 0x19 */ u8 mArrow;
    /* 0x1A */ u8 field_0x1a;
};

static daTit_HIO_c g_daTitHIO;

static u8 const lit_3772[12] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

#if VERSION == VERSION_GCN_PAL
static char const l_arcName[] = "TitlePal";
#else
static char const l_arcName[] = "Title";
#endif

daTit_HIO_c::daTit_HIO_c() {
    mPSScaleX = 1.0f;
    mPSScaleY = 1.0f;

    #if VERSION == VERSION_GCN_PAL
    switch (OSGetLanguage()) {
    case OS_LANGUAGE_ENGLISH:
    case OS_LANGUAGE_GERMAN:
    case OS_LANGUAGE_SPANISH:
    case OS_LANGUAGE_ITALIAN:
    case OS_LANGUAGE_DUTCH:
        mPSPosX = 303.0f;
        break;
    case OS_LANGUAGE_FRENCH:
        mPSPosX = FB_WIDTH / 2;
        break;
    }
    #else
    mPSPosX = 303.0f;
    #endif

    mPSPosY = 347.0f;
    mAppear = 15;
    mArrow = 60;
    field_0x1a = 15;
}

#if DEBUG
void daTit_HIO_c::genMessage(JORMContext* mctx) {
    mctx->genLabel("\n======= PRESS START ========", 0);
    mctx->genSlider("Scale Ｘ", &mPSScaleX, 0.1f, 100.0f);
    mctx->genSlider("Scale Ｙ", &mPSScaleY, 0.1f, 100.0f);
    mctx->genSlider("Pos Ｘ", &mPSPosX, 0.0f, 1000.0f);
    mctx->genSlider("Pos Ｙ", &mPSPosY, 0.0f, 1000.0f);

    mctx->genLabel("\n======= ", 0);
    mctx->genSlider("出現", &mAppear, 0, 255);
    mctx->genSlider("矢印", &mArrow, 0, 255);
}
#endif

int daTitle_c::CreateHeap() {
#ifdef TARGET_PC
    fprintf(stderr, "[TITLE] CreateHeap: begin\n"); fflush(stderr);
#endif
    J3DModelData* modelData = (J3DModelData*)dComIfG_getObjectRes(l_arcName, 10);
    JUT_ASSERT(258, modelData);
#ifdef TARGET_PC
    fprintf(stderr, "[TITLE] CreateHeap: modelData=%p\n", modelData); fflush(stderr);
#endif
    mpModel = mDoExt_J3DModel__create(modelData, 0x80000, 0x11000285);

    if (mpModel == NULL) {
#ifdef TARGET_PC
        fprintf(stderr, "[TITLE] CreateHeap: mDoExt_J3DModel__create FAILED\n"); fflush(stderr);
#endif
        return 0;
    }
#ifdef TARGET_PC
    fprintf(stderr, "[TITLE] CreateHeap: model=%p\n", mpModel); fflush(stderr);
#endif

    int res = mBck.init((J3DAnmTransform*)dComIfG_getObjectRes(l_arcName, 7), 1, 0, 2.0f, 0, -1, false);
    JUT_ASSERT(276, res == 1);
#ifdef TARGET_PC
    fprintf(stderr, "[TITLE] CreateHeap: BCK ok\n"); fflush(stderr);
#endif

    res = mBpk.init(modelData, (J3DAnmColor*)dComIfG_getObjectRes(l_arcName, 13), 1, 0, 2.0f, 0, -1);
    JUT_ASSERT(283, res == 1);
#ifdef TARGET_PC
    fprintf(stderr, "[TITLE] CreateHeap: BPK ok\n"); fflush(stderr);
#endif

    res = mBrk.init(modelData, (J3DAnmTevRegKey*)dComIfG_getObjectRes(l_arcName, 16), 1, 0, 2.0f, 0, -1);
    JUT_ASSERT(290, res == 1);
#ifdef TARGET_PC
    fprintf(stderr, "[TITLE] CreateHeap: BRK ok\n"); fflush(stderr);
#endif

    res = mBtk.init(modelData, (J3DAnmTextureSRTKey*)dComIfG_getObjectRes(l_arcName, 19), 1, 0, 2.0f, 0, -1);
    JUT_ASSERT(297, res == 1);
#ifdef TARGET_PC
    fprintf(stderr, "[TITLE] CreateHeap: BTK ok\n"); fflush(stderr);
#endif

    return 1;
}

static procFunc daTitleProc[6] = {
    &daTitle_c::loadWait_proc, &daTitle_c::logoDispWait, &daTitle_c::logoDispAnm,
    &daTitle_c::keyWait, &daTitle_c::nextScene_proc, &daTitle_c::fastLogoDisp,
};

int daTitle_c::create() {
    fopAcM_ct(this, daTitle_c);

    int phase_state = dComIfG_resLoad(&mPhaseReq, l_arcName);
#ifdef TARGET_PC
    static int s_tc = 0;
    if (s_tc++ < 5) fprintf(stderr, "[TITLE] create: resLoad('%s') = %d\n", l_arcName, phase_state);
#endif
    if (phase_state != cPhs_COMPLEATE_e) {
        return phase_state;
    }

#ifdef TARGET_PC
    fprintf(stderr, "[TITLE] create: entrySolidHeap...\n"); fflush(stderr);
#endif
    if (!fopAcM_entrySolidHeap(this, createHeapCallBack, 0x4000)) {
#ifdef TARGET_PC
        fprintf(stderr, "[TITLE] create: entrySolidHeap FAILED — returning ERROR\n");
#endif
        return cPhs_ERROR_e;
    }
#ifdef TARGET_PC
    fprintf(stderr, "[TITLE] create: heap OK, mounting Title2D.arc...\n"); fflush(stderr);
#endif

    mpMount = mDoDvdThd_mountArchive_c::create("/res/Layout/Title2D.arc", 0, NULL);
#ifdef TARGET_PC
    if (mpMount == NULL) {
        fprintf(stderr, "[TITLE] WARNING: Title2D.arc mount returned NULL\n");
    } else if (mpMount->getArchive() == NULL) {
        fprintf(stderr, "[TITLE] WARNING: Title2D.arc mounted but archive is NULL\n");
    } else {
        fprintf(stderr, "[TITLE] Title2D.arc mounted OK: archive=%p\n", (void*)mpMount->getArchive());
    }
#endif
    mIsDispLogo = 0;
    field_0x5f9 = 0;

    m2DHeap = JKRCreateExpHeap(0x8000, mDoExt_getGameHeap(), false);
    JUT_ASSERT(345, m2DHeap != NULL);
    loadWait_init();

    g_daTitHIO.id = mDoHIO_CREATE_CHILD("タイトルロゴ", &g_daTitHIO);

    return phase_state;
}

int daTitle_c::createHeapCallBack(fopAc_ac_c* actor) {
    daTitle_c* i_this = (daTitle_c*)actor;
    return i_this->CreateHeap();
}

int daTitle_c::Execute() {
#ifdef TARGET_PC
    static int s_te = 0;
    if (s_te++ < 3) fprintf(stderr, "[TITLE] Execute() mProcID=%d\n", mProcID);
    if (mProcID < 0 || mProcID >= (int)(sizeof(daTitleProc) / sizeof(daTitleProc[0]))) {
        fprintf(stderr, "[TITLE] Execute: invalid mProcID=%d, resetting to keyWait\n", mProcID);
        mProcID = 3;
    }
#endif
    #if PLATFORM_WII || PLATFORM_SHIELD
    mDoGph_gInf_c::resetDimming();
    #endif

    if (fopOvlpM_IsPeek()) {
        return 1;
    }

    dMenu_Collect3D_c::setViewPortOffsetY(0.0f);

    if (mDoRst::isReset()) {
        return 1;
    }

    (this->*daTitleProc[mProcID])();
    KeyWaitAnm();

    #if VERSION == VERSION_SHIELD_DEBUG
    KeyWaitPosMove();
    #endif

    return 1;
}

void daTitle_c::KeyWaitAnm() {
    if (field_0x600 == NULL) {
        return;
    }
    if (field_0x5f9 != 0) {
        if (field_0x604 == 0) {
            if (field_0x5fa != 0) {
                field_0x600->alphaAnime(g_daTitHIO.mArrow, 0, 255, 0);
            } else {
                field_0x600->alphaAnimeLoop(g_daTitHIO.mArrow, 255, 128, 0);
            }

            if (field_0x600->getAlpha() == 255) {
                if (field_0x5fa != 0) {
                    field_0x5fa = 0;
                }
                field_0x604 = g_daTitHIO.field_0x1a;
            }
        }

        if (field_0x604 != 0) {
            field_0x604--;
        }
    }
}

#if VERSION == VERSION_SHIELD_DEBUG
void daTitle_c::KeyWaitPosMove() {
    J2DPane* pane = mTitle.Scr->search(MULTI_CHAR('n_all'));
    pane->translate(g_daTitHIO.mPSPosX, g_daTitHIO.mPSPosY);
    pane->scale(g_daTitHIO.mPSScaleX, g_daTitHIO.mPSScaleY);
}
#endif

void daTitle_c::loadWait_init() {
    mProcID = 0;
}

void daTitle_c::loadWait_proc() {
#ifdef TARGET_PC
    if (mpMount == NULL || mpMount->sync()) {
        /* On PC, skip J2D setup — .blo binary layout has 32/64-bit struct
         * alignment mismatch that prevents pane parsing. Skip directly to
         * the 3D model display. TODO: fix J2DPaneInfo struct layout for 64-bit. */
        mIsDispLogo = 0;  /* Don't try to draw 2D overlay */
        logoDispAnmInit();  /* Skip straight to logo animation */
        return;
    }
    return; /* Still loading */
#else
    if (mpMount == NULL) return;
#endif
    if (mpMount->sync()) {
#ifdef TARGET_PC
        fprintf(stderr, "[TITLE] loadWait: sync OK, setting up 2D...\n"); fflush(stderr);
#endif
        mpHeap = mDoExt_setCurrentHeap(m2DHeap);

        mpFont = mDoExt_getMesgFont();
#ifdef TARGET_PC
        fprintf(stderr, "[TITLE] loadWait: font=%p, creating J2DScreen...\n", (void*)mpFont); fflush(stderr);
#endif

        mTitle.Scr = new J2DScreen();
        JUT_ASSERT(529, mTitle.Scr != NULL);

#ifdef TARGET_PC
        fprintf(stderr, "[TITLE] loadWait: setPriority...\n"); fflush(stderr);
#endif
        mTitle.Scr->setPriority("zelda_press_start.blo", 0x100000, mpMount->getArchive());

#ifdef TARGET_PC
        fprintf(stderr, "[TITLE] loadWait: searching panes...\n"); fflush(stderr);
#endif
        J2DTextBox* text[7];
        text[0] = (J2DTextBox*)mTitle.Scr->search(MULTI_CHAR('t_s_00'));
        text[1] = (J2DTextBox*)mTitle.Scr->search(MULTI_CHAR('t_s_01'));
        text[2] = (J2DTextBox*)mTitle.Scr->search(MULTI_CHAR('t_s_02'));
        text[3] = (J2DTextBox*)mTitle.Scr->search(MULTI_CHAR('t_s_03'));
        text[4] = (J2DTextBox*)mTitle.Scr->search(MULTI_CHAR('t_s_04'));
        text[5] = (J2DTextBox*)mTitle.Scr->search(MULTI_CHAR('t_s_05'));
        text[6] = (J2DTextBox*)mTitle.Scr->search('t_o');

#ifdef TARGET_PC
        fprintf(stderr, "[TITLE] loadWait: panes: %p %p %p %p %p %p %p\n",
                (void*)text[0], (void*)text[1], (void*)text[2], (void*)text[3],
                (void*)text[4], (void*)text[5], (void*)text[6]);
        fflush(stderr);
#endif
        for (int i = 0; i < 7; i++) {
#ifdef TARGET_PC
            if (text[i] == NULL) {
                fprintf(stderr, "[TITLE] loadWait: text[%d] is NULL — skipping\n", i);
                continue;
            }
#endif
            text[i]->setFont(mpFont);
            text[i]->setString(0x80, "");

            char* msg = text[i]->getStringPtr();
            fopMsgM_messageGet(msg, 100);
        }

        field_0x600 = new CPaneMgrAlpha(mTitle.Scr, MULTI_CHAR('n_all'), 2, NULL);
        field_0x600->setAlpha(0);
        J2DPane* pane = mTitle.Scr->search(MULTI_CHAR('n_all'));
        pane->translate(g_daTitHIO.mPSPosX, g_daTitHIO.mPSPosY);
        pane->scale(g_daTitHIO.mPSScaleX, g_daTitHIO.mPSScaleY);
        JKRSetCurrentHeap(mpHeap);
        logoDispWaitInit();
    }
}

void daTitle_c::logoDispWaitInit() {
    mProcID = 1;
}

void daTitle_c::logoDispWait() {
    if (mDoCPd_c::getTrigA(PAD_1) || mDoCPd_c::getTrigStart(PAD_1)) {
        fastLogoDispInit();
    } else if (getDemoPrm() == 1) {
        logoDispAnmInit();
    }
}

void daTitle_c::logoDispAnmInit() {
    mBck.setPlaySpeed(1.0f);
    mBpk.setPlaySpeed(1.0f);
    mBrk.setPlaySpeed(1.0f);
    mBtk.setPlaySpeed(1.0f);
    mIsDispLogo = 1;
    mProcID = 2;
}

void daTitle_c::logoDispAnm() {
    mBck.play();
    mBpk.play();
    mBrk.play();
    mBtk.play();

    if (mBrk.isStop() && mBtk.isStop() && mBck.isStop() && mBpk.isStop()) {
        if (field_0x600 != NULL) {
            field_0x600->alphaAnimeStart(0);
        }
        field_0x604 = 0;
        field_0x5f9 = 1;
        field_0x5fa = 1;
        keyWaitInit();
    }
}

void daTitle_c::keyWaitInit() {
    mProcID = 3;
}

void daTitle_c::keyWait() {
    if (mDoCPd_c::getTrigA(PAD_1) || mDoCPd_c::getTrigStart(PAD_1)) {
        mDoAud_seStart(Z2SE_TITLE_ENTER, NULL, 0, 0);
        nextScene_init();
    }
}

void daTitle_c::nextScene_init() {
    mProcID = 4;
}

void daTitle_c::nextScene_proc() {
    scene_class* playScene;

    if (!fopOvlpM_IsPeek() && !mDoRst::isReset()) {
        playScene = fopScnM_SearchByID(dStage_roomControl_c::getProcID());
        JUT_ASSERT(706, playScene != NULL);

        #if DEBUG
        if (!dScnLogo_c::isOpeningCut())
        #endif
        {
            fopScnM_ChangeReq(playScene, fpcNm_NAME_SCENE_e, 0, 5);
        }
        #if DEBUG
        else {
            fopScnM_ChangeReq(playScene, fpcNm_MENU_SCENE_e, 0, 5);
            dComIfGs_init();
            dComIfG_playerStatusD();
        }
        #endif

        #if VERSION != VERSION_SHIELD_DEBUG
        mDoGph_gInf_c::setFadeColor(*(JUtility::TColor*)&g_blackColor);
        #endif
    }
}

void daTitle_c::fastLogoDispInit() {
    mBck.setFrame(mBck.getEndFrame() - 1.0f);
    mBpk.setFrame(mBpk.getEndFrame() - 1.0f);
    mBrk.setFrame(mBrk.getEndFrame() - 1.0f);
    mBtk.setFrame(mBtk.getEndFrame() - 1.0f);

    if (field_0x600 != NULL) {
        field_0x600->alphaAnimeStart(0);
    }
    field_0x604 = 0;
    mWaitTimer = 30;
    mProcID = 5;
}

void daTitle_c::fastLogoDisp() {
    if (mWaitTimer != 0) {
        mWaitTimer--;
        return;
    }

    field_0x5f9 = 1;
    field_0x5fa = 1;
    mIsDispLogo = 1;
    keyWaitInit();
}

int daTitle_c::getDemoPrm() {
    dDemo_actor_c* demoActor = dDemo_c::getActor(demoActorID);
    dDemo_prm_c* prm;
    if (demoActor != NULL && demoActor->checkEnable(1) && (prm = demoActor->getPrm())) {
        JStudio::stb::TParseData_fixed<49> parser(prm->getData());
        TValueIterator_raw<u8> iter = parser.begin();
        return *iter;
    }

    return -1;
}

int daTitle_c::Draw() {
#ifdef TARGET_PC
    static int s_td = 0;
    if (s_td++ < 3) fprintf(stderr, "[TITLE] Draw #%d: mpModel=%p\n", s_td, (void*)mpModel);
    if (mpModel == NULL) {
        if (s_td < 16) {
            fprintf(stderr, "[TITLE] Draw: mpModel is NULL, skipping draw\n");
            fflush(stderr);
        }
        return 1;
    }
#endif
    J3DModelData* modelData = mpModel->getModelData();
#ifdef TARGET_PC
    if (modelData == NULL || mpModel->getBaseTRMtx() == NULL) {
        if (s_td < 16) {
            fprintf(stderr, "[TITLE] Draw: invalid model internals data=%p baseMtx=%p, skipping\n",
                    (void*)modelData, (void*)mpModel->getBaseTRMtx());
            fflush(stderr);
        }
        return 1;
    }
#endif
    cMtx_trans(mpModel->getBaseTRMtx(), IREG_F(7), IREG_F(8), IREG_F(9) + -430.0f);
    mpModel->getBaseScale()->x = -1.0f;

#ifdef TARGET_PC
    /* Skip all animation entry on PC — animation data has byte-swap issues
     * that corrupt material/TEV state on repeated calls.
     * TODO: fix J3DAnm byte-swapping for BCK/BPK/BRK/BTK */
    if (s_td <= 3) { fprintf(stderr, "[TITLE] Draw: skipping anm entry (PC)\n"); fflush(stderr); }
#else
    mBck.entry(modelData);
    mBpk.entry(modelData);
    mBrk.entry(modelData);
    mBtk.entry(modelData);
#endif

#ifdef TARGET_PC
    if (s_td <= 3) { fprintf(stderr, "[TITLE] Draw: setListItem3D...\n"); fflush(stderr); }
#endif
    dComIfGd_setListItem3D();
#ifdef TARGET_PC
    if (s_td <= 3) { fprintf(stderr, "[TITLE] Draw: modelUpdateDL...\n"); fflush(stderr); }
#endif
    mDoExt_modelUpdateDL(mpModel);
#ifdef TARGET_PC
    if (s_td <= 3) { fprintf(stderr, "[TITLE] Draw: setList...\n"); fflush(stderr); }
#endif
    dComIfGd_setList();

#ifdef TARGET_PC
    /* Skip 2D overlay until J2D parsing is fixed */
#else
    if (mIsDispLogo) {
        dComIfGd_set2DOpaTop(&mTitle);
    }
#endif

    return 1;
}

int daTitle_c::Delete() {
#ifdef TARGET_PC
    fprintf(stderr, "[TITLE] Delete()\n"); fflush(stderr);
#endif
    mDoHIO_DELETE_CHILD(g_daTitHIO.id);

    dComIfG_resDelete(&mPhaseReq, l_arcName);
    if (mTitle.Scr != NULL) {
        delete mTitle.Scr;
        mTitle.Scr = NULL;
    }
    if (field_0x600 != NULL) {
        delete field_0x600;
        field_0x600 = NULL;
    }

    if (mpMount != NULL) {
        JKRArchive* archive = mpMount->getArchive();
        if (archive != NULL) {
            archive->removeResourceAll();
            JKRUnmountArchive(archive);
        }
        mpMount->destroy();
        mpMount = NULL;
    }

    if (m2DHeap != NULL) {
        m2DHeap->destroy();
        m2DHeap = NULL;
    }

    return 1;
}

static int daTitle_Draw(daTitle_c* i_this) {
    return i_this->Draw();
}

static int daTitle_Execute(daTitle_c* i_this) {
    return i_this->Execute();
}

static int daTitle_Delete(daTitle_c* i_this) {
    fpc_ProcID id = fopAcM_GetID(i_this);
    return i_this->Delete();
}

static int daTitle_Create(fopAc_ac_c* i_this) {
    daTitle_c* a_this = (daTitle_c*)i_this;
    fpc_ProcID id = fopAcM_GetID(i_this);
    return a_this->create();
}

void dDlst_daTitle_c::draw() {
    J2DGrafContext* ctx = dComIfGp_getCurrentGrafPort();
    Scr->draw(0.0f, 0.0f, ctx);
}

static actor_method_class l_daTitle_Method = {
    (process_method_func)daTitle_Create,
    (process_method_func)daTitle_Delete,
    (process_method_func)daTitle_Execute,
    (process_method_func)NULL,
    (process_method_func)daTitle_Draw,
};

actor_process_profile_definition g_profile_TITLE = {
    /* Layer ID     */ fpcLy_CURRENT_e,
    /* List ID      */ 7,
    /* List Prio    */ fpcPi_CURRENT_e,
    /* Proc Name    */ fpcNm_TITLE_e,
    /* Proc SubMtd  */ &g_fpcLf_Method.base,
    /* Size         */ sizeof(daTitle_c),
    /* Size Other   */ 0,
    /* Parameters   */ 0,
    /* Leaf SubMtd  */ &g_fopAc_Method.base,
    /* Draw Prio    */ fpcDwPi_TITLE_e,
    /* Actor SubMtd */ &l_daTitle_Method,
    /* Status       */ fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e,
    /* Group        */ fopAc_ACTOR_e,
    /* Cull Type    */ fopAc_CULLBOX_CUSTOM_e,
};
