/**
 * f_op_overlap_req.cpp
 * Overlap Process Request
 */

#include "SSystem/SComponent/c_request.h"
#include "f_op/f_op_overlap_req.h"
#include "d/d_com_inf_game.h"
#include "f_pc/f_pc_manager.h"
#ifdef TARGET_PC
#include <cstdio>
#include "dolphin/vi.h"
#endif

void fopOvlpReq_SetPeektime(overlap_request_class*, u16);

static int fopOvlpReq_phase_Done(overlap_request_class* i_overlapReq) {
    if (fpcM_Delete(i_overlapReq->overlap_task) == 1) {
#ifdef TARGET_PC
        fprintf(stderr, "[OVLP] phase_Done: request_id=%u\n", (unsigned)i_overlapReq->request_id);
#endif
        i_overlapReq->overlap_task = NULL;
        i_overlapReq->field_0x4 = 0;
        i_overlapReq->peektime = 0;
        i_overlapReq->field_0x8 = 0;
        i_overlapReq->field_0xc = 0;
        return cPhs_NEXT_e;
    }

    return cPhs_INIT_e;
}

static s32 fopOvlpReq_phase_IsDone(overlap_request_class* i_overlapReq) {
    cReq_Done(&i_overlapReq->base);
#ifdef TARGET_PC
    static int s_log_count = 0;
    if (s_log_count++ < 20 || i_overlapReq->field_0x2 <= 1) {
        fprintf(stderr, "[OVLP] phase_IsDone: field_0x2=%d\n", i_overlapReq->field_0x2);
    }
#endif
    if (i_overlapReq->field_0x2-- <= 0) {
        return cPhs_NEXT_e;
    }

    return cPhs_INIT_e;
}

static int fopOvlpReq_phase_IsWaitOfFadeout(overlap_request_class* i_overlapReq) {
    if (cReq_Is_Done(&i_overlapReq->overlap_task->request)) {
#ifdef TARGET_PC
        fprintf(stderr, "[OVLP] phase_IsWaitOfFadeout: overlap task done\n");
#endif
        i_overlapReq->field_0x8 = 0;
        return cPhs_NEXT_e;
    }

    return cPhs_INIT_e;
}

static int fopOvlpReq_phase_WaitOfFadeout(overlap_request_class* i_overlapReq) {
    if (i_overlapReq->peektime) {
#ifdef TARGET_PC
        /* On PC, heavy loading can cause this phase handler to run much less often
         * than once per retrace. Consume peektime by retrace delta to preserve
         * original timing semantics and avoid transition stalls. */
        u32 now = VIGetRetraceCount();
        if (i_overlapReq->field_0xc == 0) {
            i_overlapReq->field_0xc = now;
        }
        u32 delta = now - i_overlapReq->field_0xc;
        if (delta == 0) {
            delta = 1;
        }
        i_overlapReq->peektime = (i_overlapReq->peektime > delta) ? (i_overlapReq->peektime - delta) : 0;
        i_overlapReq->field_0xc = now;
#else
        i_overlapReq->peektime--;
#endif
    }

#ifdef TARGET_PC
    if (strcmp(dComIfGp_getStartStageName(), "F_SP102") == 0 &&
        (dComIfGp_getStartStagePoint() == 100 || i_overlapReq->peektime <= 20))
    {
        cReq_Command(&i_overlapReq->overlap_task->request, 2);
        return cPhs_NEXT_e;
    }

    static int s_log_count = 0;
    if (s_log_count++ < 30 || i_overlapReq->peektime == 0) {
        fprintf(stderr, "[OVLP] phase_WaitOfFadeout: flag2=%d peek=%u done=%d\n",
                i_overlapReq->base.flag2, i_overlapReq->peektime,
                cReq_Is_Done(&i_overlapReq->overlap_task->request));
    }
#endif
    if (i_overlapReq->base.flag2 == 2 && !i_overlapReq->peektime) {
        cReq_Command(&i_overlapReq->overlap_task->request, 2);
#ifdef TARGET_PC
        fprintf(stderr, "[OVLP] phase_WaitOfFadeout: sent out request\n");
#endif
        return cPhs_NEXT_e;
    }

    i_overlapReq->field_0x8 = 1;
    return cPhs_INIT_e;
}

static int fopOvlpReq_phase_IsComplete(overlap_request_class* i_overlapReq) {
    if (cReq_Is_Done(&i_overlapReq->overlap_task->request)) {
#ifdef TARGET_PC
        fprintf(stderr, "[OVLP] phase_IsComplete: overlap task complete\n");
#endif
        cReq_Done(&i_overlapReq->base);
        return cPhs_NEXT_e;
    }

    return cPhs_INIT_e;
}

static int fopOvlpReq_phase_IsCreated(overlap_request_class* i_overlapReq) {
    if (fpcM_IsCreating(i_overlapReq->request_id) == 0) {
        overlap_task_class* process = (overlap_task_class*)fpcM_SearchByID(i_overlapReq->request_id);
        if (process == NULL) {
#ifdef TARGET_PC
            fprintf(stderr, "[OVLP] phase_IsCreated: request_id=%u missing process\n",
                    (unsigned)i_overlapReq->request_id);
#endif
            return cPhs_ERROR_e;
        }
    
        i_overlapReq->overlap_task = process;
#ifdef TARGET_PC
        fprintf(stderr, "[OVLP] phase_IsCreated: request_id=%u process=%p\n",
                (unsigned)i_overlapReq->request_id, (void*)process);
#endif
        return cPhs_NEXT_e;
    }

    return cPhs_INIT_e;
}

static int fopOvlpReq_phase_Create(overlap_request_class* i_overlapReq) {
    fpcLy_SetCurrentLayer(i_overlapReq->layer);
    i_overlapReq->request_id =
        fpcM_Create(i_overlapReq->procname, NULL, NULL);
#ifdef TARGET_PC
    fprintf(stderr, "[OVLP] phase_Create: proc=%d request_id=%u\n",
            i_overlapReq->procname, (unsigned)i_overlapReq->request_id);
#endif
    return cPhs_NEXT_e;
}

overlap_request_class* fopOvlpReq_Request(overlap_request_class* i_overlapReq, s16 i_procname,
                                       u16 i_peektime) {
    static cPhs__Handler phaseMethod[8] = {
        (cPhs__Handler)fopOvlpReq_phase_Create,
        (cPhs__Handler)fopOvlpReq_phase_IsCreated,
        (cPhs__Handler)fopOvlpReq_phase_IsComplete,
        (cPhs__Handler)fopOvlpReq_phase_WaitOfFadeout,
        (cPhs__Handler)fopOvlpReq_phase_IsWaitOfFadeout,
        (cPhs__Handler)fopOvlpReq_phase_IsDone,
        (cPhs__Handler)fopOvlpReq_phase_Done,
        (cPhs__Handler)NULL,
    };

    if (i_overlapReq->field_0x4 == 1) {
        return NULL;
    }

    cReq_Command(&i_overlapReq->base, 1);
    i_overlapReq->procname = i_procname;
    cPhs_Set(&i_overlapReq->phase_req, phaseMethod);
    fopOvlpReq_SetPeektime(i_overlapReq, i_peektime);
    i_overlapReq->field_0x4 = 1;
    i_overlapReq->field_0x2 = 1;
    i_overlapReq->overlap_task = NULL;
    i_overlapReq->field_0x8 = 0;
    i_overlapReq->field_0xc = 0;
    i_overlapReq->layer = fpcLy_RootLayer();
    return i_overlapReq;
}

int fopOvlpReq_Handler(overlap_request_class* i_overlapReq) {
    switch (cPhs_Do(&i_overlapReq->phase_req, i_overlapReq)) {
    case cPhs_NEXT_e:
        return fopOvlpReq_Handler(i_overlapReq);
    case cPhs_INIT_e:
        return cPhs_INIT_e;
    case cPhs_LOADING_e:
        return cPhs_INIT_e;
    case cPhs_COMPLEATE_e:
        return cPhs_COMPLEATE_e;
    case cPhs_UNK3_e:
    case cPhs_ERROR_e:
        return cPhs_ERROR_e;
    default:
        return cPhs_ERROR_e;
    }
}

int fopOvlpReq_Cancel(overlap_request_class* i_overlapReq) {
    if (fopOvlpReq_phase_Done(i_overlapReq) == cPhs_NEXT_e) {
        return TRUE;
    }
    return FALSE;
}

int fopOvlpReq_Is_PeektimeLimit(overlap_request_class* i_overlapReq) {
    if (i_overlapReq->peektime == 0) {
        return TRUE;
    }
    return FALSE;
}

void fopOvlpReq_SetPeektime(overlap_request_class* i_overlapReq, u16 i_peektime) {
    if (i_peektime <= 0x7FFF) {
        i_overlapReq->peektime = i_peektime;
    }
}

int fopOvlpReq_OverlapClr(overlap_request_class* i_overlapReq) {
    if (i_overlapReq->base.flag0 == 1 || !fopOvlpReq_Is_PeektimeLimit(i_overlapReq))
    {
        return 0;
    }

    cReq_Create(&i_overlapReq->base, 2);
    return 1;
}
