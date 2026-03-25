/**
 * f_op_scene_req.cpp
 * Scene Process Request
 */

#include "f_op/f_op_scene_req.h"
#ifdef TARGET_PC
#include <cstdio>
#endif
#include "f_op/f_op_overlap_mng.h"
#include "f_op/f_op_scene.h"
#include "f_op/f_op_scene_pause.h"
#include "f_pc/f_pc_executor.h"
#include "f_pc/f_pc_manager.h"

static cPhs_Step fopScnRq_phase_ClearOverlap(scene_request_class* i_sceneReq) {
    int cleared = fopOvlpM_ClearOfReq();
#ifdef TARGET_PC
    static int s_log_count = 0;
    if ((cleared == 1 || s_log_count++ < 10) && i_sceneReq->fade_request != NULL) {
        fprintf(stderr, "[SCNRQ] phase_ClearOverlap: cleared=%d\n", cleared);
    }
#endif
    return cleared == 1 ? cPhs_NEXT_e : cPhs_INIT_e;
    UNUSED(i_sceneReq);
}

static cPhs_Step fopScnRq_phase_Execute(scene_request_class* i_sceneReq) {
#ifdef TARGET_PC
    static int s_execute_log = 0;
    if (s_execute_log++ < 10) {
        fprintf(stderr, "[SCNRQ] phase_Execute: name=%d params=%d fade=%p\n",
                i_sceneReq->create_request.name, i_sceneReq->create_request.parameters,
                (void*)i_sceneReq->fade_request);
    }
#endif
    return fpcNdRq_Execute(&i_sceneReq->create_request);
}

static cPhs_Step fopScnRq_phase_IsDoingOverlap(scene_request_class* i_sceneReq) {
    int doing = fopOvlpM_IsDoingReq();
#ifdef TARGET_PC
    static int s_log_count = 0;
    if ((doing == 1 || s_log_count++ < 20) && i_sceneReq->fade_request != NULL) {
        fprintf(stderr, "[SCNRQ] phase_IsDoingOverlap: doing=%d\n", doing);
    }
#endif
    return doing == 1 ? cPhs_NEXT_e : cPhs_INIT_e;
    UNUSED(i_sceneReq);
}

static cPhs_Step fopScnRq_phase_IsDoneOverlap(scene_request_class* i_sceneReq) {
    int done = fopOvlpM_IsDone();
#ifdef TARGET_PC
    static int s_log_count = 0;
    if ((done == 1 || s_log_count++ < 40) && i_sceneReq->fade_request != NULL) {
        fprintf(stderr, "[SCNRQ] phase_IsDoneOverlap: done=%d\n", done);
    }
#endif
    return done == 1 ? cPhs_NEXT_e : cPhs_INIT_e;
    UNUSED(i_sceneReq);
}

static BOOL l_fopScnRq_IsUsingOfOverlap;

static cPhs_Step fopScnRq_phase_Done(scene_request_class* i_sceneReq) {
#ifdef TARGET_PC
    fprintf(stderr, "[PC] fopScnRq_phase_Done: params=%d creating_id=%u\n",
            i_sceneReq->create_request.parameters, (unsigned)i_sceneReq->create_request.creating_id);
#endif
    if (i_sceneReq->create_request.parameters != 1) {
        scene_class* scene = (scene_class*)fpcM_SearchByID(i_sceneReq->create_request.creating_id);
#ifdef TARGET_PC
        /* Some PC handoffs report a transient creating_id that is not the final scene.
         * Fall back to process-name lookup so the new scene is actually unpaused. */
        scene_class* sceneByName = (scene_class*)fpcM_SearchByName(i_sceneReq->create_request.name);
        if (scene == NULL) {
            scene = sceneByName;
        }
#endif
#ifdef TARGET_PC
        fprintf(stderr, "[PC] fopScnRq_phase_Done: scene=%p, calling fopScnPause_Disable\n", (void*)scene);
#endif
        (void)scene;
        fopScnPause_Disable(scene);
#ifdef TARGET_PC
        if (sceneByName != NULL && sceneByName != scene) {
            fprintf(stderr, "[PC] fopScnRq_phase_Done: also disabling by-name scene=%p name=%d\n",
                    (void*)sceneByName, i_sceneReq->create_request.name);
            fopScnPause_Disable(sceneByName);
        }
#endif
    }

    l_fopScnRq_IsUsingOfOverlap = FALSE;
    return cPhs_NEXT_e;
}

static cPhs_Step fopScnRq_Execute(scene_request_class* i_sceneReq) {
    cPhs_Step phase_state = cPhs_Do(&i_sceneReq->phase_request, i_sceneReq);

    switch (phase_state) {
    case cPhs_NEXT_e:
        return fopScnRq_Execute(i_sceneReq);
        break;
    default:
        break;
    }

    return phase_state;
}

static int fopScnRq_PostMethod(void* i_scene, scene_request_class* i_sceneReq) {
    if (i_sceneReq->fade_request != NULL) {
        fopScnPause_Enable((scene_class*)i_scene);
        fopOvlpM_ToldAboutID(((scene_class*)i_scene)->base.base.id);
    }
    return 1;
}

static int fopScnRq_Cancel(scene_request_class* i_sceneReq) {
    if (i_sceneReq->fade_request != NULL && !fopOvlpM_Cancel()) {
        return 0;
    } else {
        return 1;
    }
}

static scene_request_class* fopScnRq_FadeRequest(s16 i_procname, u16 i_peektime) {
    overlap_request_class* req = NULL;

    if (!l_fopScnRq_IsUsingOfOverlap) {
        req = fopOvlpM_Request(i_procname, i_peektime);
        if (req != NULL) {
            l_fopScnRq_IsUsingOfOverlap = TRUE;
#ifdef TARGET_PC
            fprintf(stderr, "[SCNRQ] FadeRequest: proc=%d peek=%u req=%p\n",
                    i_procname, i_peektime, (void*)req);
#endif
        }
    }

    return (scene_request_class*)req;
}

fpc_ProcID fopScnRq_Request(int i_reqType, scene_class* i_scene, s16 i_procName, void* i_data, s16 i_fadename,
                     u16 i_peektime) {
    static node_create_request_method_class submethod = {
        (process_method_func)fopScnRq_Execute,
        (process_method_func)fopScnRq_Cancel,
        NULL,
        (process_method_func)fopScnRq_PostMethod,
    };

    static cPhs__Handler noFadeFase[8] = {
        (cPhs__Handler)fopScnRq_phase_Execute,
        (cPhs__Handler)fopScnRq_phase_Done,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
    };

    static cPhs__Handler fadeFase[8] = {
        (cPhs__Handler)fopScnRq_phase_IsDoingOverlap,
        (cPhs__Handler)fopScnRq_phase_IsDoneOverlap,
        (cPhs__Handler)fopScnRq_phase_Execute,
        (cPhs__Handler)fopScnRq_phase_ClearOverlap,
        (cPhs__Handler)fopScnRq_phase_IsDoneOverlap,
        (cPhs__Handler)fopScnRq_phase_Done,
        NULL,
        NULL,
    };

    scene_request_class* fade_req = NULL;
    cPhs__Handler* phase_handler = noFadeFase;

    scene_request_class* req = (scene_request_class*)fpcNdRq_Request(
        sizeof(scene_request_class), i_reqType, (process_node_class*)i_scene, i_procName, i_data,
        &submethod);

    if (req == NULL) {
        return fpcM_ERROR_PROCESS_ID_e;
    }

    if (i_fadename != 0x7FFF) {
        phase_handler = fadeFase;
        fade_req = fopScnRq_FadeRequest(i_fadename, i_peektime);
        if (fade_req == NULL) {
            fpcNdRq_Delete(&req->create_request);
            return fpcM_ERROR_PROCESS_ID_e;
        }
    }

#ifdef TARGET_PC
    fprintf(stderr, "[SCNRQ] Request: type=%d proc=%d fade=%d peek=%u req=%p fadeReq=%p\n",
            i_reqType, i_procName, i_fadename, i_peektime, (void*)req, (void*)fade_req);
#endif
    req->fade_request = fade_req;
    cPhs_Set(&req->phase_request, phase_handler);
    return req->create_request.request_id;
}

s32 fopScnRq_ReRequest(fpc_ProcID i_requestId, s16 i_procName, void* i_data) {
    return fpcNdRq_ReRequest(i_requestId, i_procName, i_data);
}

int fopScnRq_Handler() {
#ifdef TARGET_PC
    static int s_handler_count = 0;
    s_handler_count++;
    int result = fpcNdRq_Handler();
    if (s_handler_count <= 3 || s_handler_count % 300 == 0)
        fprintf(stderr, "[PC] fopScnRq_Handler #%d: result=%d\n", s_handler_count, result);
    return result;
#else
    return fpcNdRq_Handler();
#endif
}
