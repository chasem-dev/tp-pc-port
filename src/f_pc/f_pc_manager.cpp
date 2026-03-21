/**
 * f_pc_manager.cpp
 * Framework - Process Manager
 */

#include "f_pc/f_pc_manager.h"
#include "SSystem/SComponent/c_API_graphic.h"
#include "SSystem/SComponent/c_lib.h"
#include "Z2AudioLib/Z2SoundMgr.h"
#include "d/d_com_inf_game.h"
#include "d/d_error_msg.h"
#include "d/d_lib.h"
#include "d/d_particle.h"
#include "f_ap/f_ap_game.h"
#include "f_pc/f_pc_creator.h"
#include "f_pc/f_pc_deletor.h"
#include "f_pc/f_pc_draw.h"
#include "f_pc/f_pc_fstcreate_req.h"
#include "f_pc/f_pc_line.h"
#include "f_pc/f_pc_pause.h"
#include "f_pc/f_pc_priority.h"
#include "m_Do/m_Do_controller_pad.h"

void fpcM_Draw(void* i_proc) {
    fpcDw_Execute((base_process_class*)i_proc);
}

int fpcM_DrawIterater(fpcM_DrawIteraterFunc i_drawIterFunc) {
    return fpcLyIt_OnlyHere(fpcLy_RootLayer(), (fpcLyIt_OnlyHereFunc)i_drawIterFunc, NULL);
}

int fpcM_Execute(void* i_proc) {
    return fpcEx_Execute((base_process_class*)i_proc);
}

int fpcM_Delete(void* i_proc) {
    return fpcDt_Delete((base_process_class*)i_proc);
}

BOOL fpcM_IsCreating(fpc_ProcID i_id) {
    return fpcCt_IsCreatingByID(i_id);
}

void fpcM_Management(fpcM_ManagementFunc i_preExecuteFn, fpcM_ManagementFunc i_postExecuteFn) {
#ifdef TARGET_PC
    static int s_mgmt_frame = 0;
    s_mgmt_frame++;
    if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] fpcM_Management: MtxInit\n");
#endif
    MtxInit();
#ifdef TARGET_PC
    if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] fpcM_Management: peekZdata\n");
#endif
    if (!fapGm_HIO_c::isCaptureScreen()) {
        dComIfGd_peekZdata();
    }
#ifdef TARGET_PC
    if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] fpcM_Management: captureScreen\n");
#endif
    fapGm_HIO_c::executeCaptureScreen();

#ifdef TARGET_PC
    if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] fpcM_Management: shutdownErr\n");
#endif
    if (!dShutdownErrorMsg_c::execute()) {
        static bool l_dvdError = false;
#ifdef TARGET_PC
        if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] fpcM_Management: dvdErr\n");
#endif
        if (!dDvdErrorMsg_c::execute()) {
            if (l_dvdError) {
                dLib_time_c::startTime();
                Z2GetSoundMgr()->pauseAllGameSound(false);
                l_dvdError = false;
            }

#ifdef TARGET_PC
            if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] fpcM_Management: cAPIGph_Painter\n");
#endif
            cAPIGph_Painter();
#ifdef TARGET_PC
            if (s_mgmt_frame == 6) fprintf(stderr, "[PC] frame6: painter done\n");
#endif

            if (!dPa_control_c::isStatus(1)) {
                fpcDt_Handler();
            } else {
                dPa_control_c::offStatus(1);
            }
#ifdef TARGET_PC
            if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] mgmt%d: dt_handler done\n", s_mgmt_frame);
#endif

#ifdef TARGET_PC
            if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] fpcM_Management: fpcPi_Handler\n");
#endif
            if (!fpcPi_Handler()) {
                JUT_ASSERT(353, FALSE);
            }
#ifdef TARGET_PC
            if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] mgmt%d: pi_handler done\n", s_mgmt_frame);
#endif

#ifdef TARGET_PC
            if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] fpcM_Management: fpcCt_Handler\n");
#endif
            if (!fpcCt_Handler()) {
                JUT_ASSERT(357, FALSE);
            }
#ifdef TARGET_PC
            if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] mgmt%d: ct_handler done\n", s_mgmt_frame);
#endif

            if (i_preExecuteFn != NULL) {
                i_preExecuteFn();
            }
#ifdef TARGET_PC
            if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] mgmt%d: preExecute done, calling fpcEx_Handler\n", s_mgmt_frame);
#endif

            if (!fapGm_HIO_c::isCaptureScreen()) {
                fpcEx_Handler((fpcLnIt_QueueFunc)fpcM_Execute);
            }
#ifdef TARGET_PC
            if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] mgmt%d: fpcEx_Handler done\n", s_mgmt_frame);
#endif
            if (!fapGm_HIO_c::isCaptureScreen() || fapGm_HIO_c::getCaptureScreenDivH() != 1) {
#ifdef TARGET_PC
                if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] mgmt%d: calling fpcDw_Handler\n", s_mgmt_frame);
#endif
                fpcDw_Handler((fpcDw_HandlerFuncFunc)fpcM_DrawIterater, (fpcDw_HandlerFunc)fpcM_Draw);
#ifdef TARGET_PC
                if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] mgmt%d: fpcDw_Handler done\n", s_mgmt_frame);
#endif
            }

            if (i_postExecuteFn != NULL) {
#ifdef TARGET_PC
                if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] mgmt%d: calling postExecute\n", s_mgmt_frame);
#endif
                i_postExecuteFn();
#ifdef TARGET_PC
                if (s_mgmt_frame <= 3) fprintf(stderr, "[PC] mgmt%d: postExecute done\n", s_mgmt_frame);
#endif
            }

            dComIfGp_drawSimpleModel();
        } else if (!l_dvdError) {
            dLib_time_c::stopTime();
            Z2GetSoundMgr()->pauseAllGameSound(true);
#if PLATFORM_GCN
#define FPCM_MANAGEMENT_GAMEPAD_COUNT 1
#elif PLATFORM_SHIELD && !DEBUG
#define FPCM_MANAGEMENT_GAMEPAD_COUNT 0
#else
#define FPCM_MANAGEMENT_GAMEPAD_COUNT 4
#endif
            for (u32 i = 0; i < FPCM_MANAGEMENT_GAMEPAD_COUNT; i++) {
                mDoCPd_c::stopMotorWaveHard(i);
            }
            l_dvdError = true;
        }
    }
}

void fpcM_Init() {
    static layer_class rootlayer;
    static node_list_class queue[10];

    fpcLy_Create(&rootlayer, NULL, queue, 10);
    fpcLn_Create();
}

base_process_class* fpcM_FastCreate(s16 i_procname, FastCreateReqFunc i_createReqFunc,
                                    void* i_createData, void* i_append) {
    return fpcFCtRq_Request(fpcLy_CurrentLayer(), i_procname, (fstCreateFunc)i_createReqFunc,
                            i_createData, i_append);
}

int fpcM_IsPause(void* i_proc, u8 i_flag) {
    return fpcPause_IsEnable((base_process_class*)i_proc, i_flag & 0xFF);
}

void fpcM_PauseEnable(void* i_proc, u8 i_flag) {
    fpcPause_Enable((process_node_class*)i_proc, i_flag & 0xFF);
}

void fpcM_PauseDisable(void* i_proc, u8 i_flag) {
    fpcPause_Disable((process_node_class*)i_proc, i_flag & 0xFF);
}

void* fpcM_JudgeInLayer(fpc_ProcID i_layerID, fpcCtIt_JudgeFunc i_judgeFunc, void* i_data) {
    layer_class* layer = fpcLy_Layer(i_layerID);
    if (layer != NULL) {
        void* ret = fpcCtIt_JudgeInLayer(i_layerID, i_judgeFunc, i_data);
        if (ret == NULL) {
            return fpcLyIt_Judge(layer, i_judgeFunc, i_data);
        }
        return ret;
    }

    return NULL;
}

