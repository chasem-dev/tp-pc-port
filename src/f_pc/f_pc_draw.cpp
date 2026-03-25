/**
 * f_pc_draw.cpp
 * Framework - Process Draw
 */

#include "f_pc/f_pc_draw.h"
#include "SSystem/SComponent/c_API_graphic.h"
#include "f_pc/f_pc_name.h"
#include "f_pc/f_pc_leaf.h"
#include "f_pc/f_pc_node.h"
#include "f_pc/f_pc_pause.h"
#ifdef TARGET_PC
#include <cstdio>
#include <csetjmp>
extern "C" void pc_crash_set_jmpbuf(jmp_buf* buf);
extern "C" jmp_buf* pc_crash_get_jmpbuf(void);
extern "C" uintptr_t pc_crash_get_addr(void);
#endif

int fpcDw_Execute(base_process_class* i_proc) {
#ifdef TARGET_PC
    static int s_dw_log = 0;
    if (s_dw_log < 120 || i_proc->profname == fpcNm_OPENING_SCENE_e || i_proc->profname == fpcNm_NAME_SCENE_e) {
        fprintf(stderr, "[DW] fpcDw_Execute: profname=%d pause=%d initState=%d\n",
                i_proc->profname, i_proc->pause_flag, i_proc->state.init_state);
        if (s_dw_log < 120) s_dw_log++;
    }
    if (i_proc->profname == fpcNm_OPENING_SCENE_e && i_proc->pause_flag != 0) {
        fprintf(stderr, "[DW] forcing OPENING_SCENE draw unpause (pause=%d)\n", i_proc->pause_flag);
        i_proc->pause_flag = 0;
    }
#endif
    if (!fpcPause_IsEnable(i_proc, 2)) {
        layer_class* save_layer;
        int ret;
        process_method_func draw_func;
    
        save_layer = fpcLy_CurrentLayer();
        if (fpcBs_Is_JustOfType(g_fpcLf_type, i_proc->subtype)) {
            draw_func = ((leafdraw_method_class*)i_proc->methods)->draw_method;
        } else {
            draw_func = ((nodedraw_method_class*)i_proc->methods)->draw_method;
        }
    
        fpcLy_SetCurrentLayer(i_proc->layer_tag.layer);
#ifdef TARGET_PC
        jmp_buf drawBuf;
        jmp_buf* prevDrawBuf = pc_crash_get_jmpbuf();
        pc_crash_set_jmpbuf(&drawBuf);
        if (setjmp(drawBuf) != 0) {
            pc_crash_set_jmpbuf(prevDrawBuf);
            static int s_draw_crash = 0;
            if (s_draw_crash++ < 10) {
                fprintf(stderr, "[DW] Draw crashed: profname=%d addr=%p\n",
                        i_proc->profname, (void*)pc_crash_get_addr());
            }
            fpcLy_SetCurrentLayer(save_layer);
            return 0;
        }
        ret = draw_func(i_proc);
        pc_crash_set_jmpbuf(prevDrawBuf);
#else
        ret = draw_func(i_proc);
#endif
        fpcLy_SetCurrentLayer(save_layer);
        return ret;
    }

    return 0;
}

int fpcDw_Handler(fpcDw_HandlerFuncFunc i_iterHandler, fpcDw_HandlerFunc i_func) {
    int ret;
    cAPIGph_BeforeOfDraw();
    ret = i_iterHandler(i_func);
    cAPIGph_AfterOfDraw();
    return ret;
}
