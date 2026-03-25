/**
 * f_pc_method.cpp
 * Framework - Process Method
 */

#include "f_pc/f_pc_method.h"
#include <csetjmp>
#include <cstdio>

extern "C" void pc_crash_set_jmpbuf(jmp_buf* buf);
extern "C" jmp_buf* pc_crash_get_jmpbuf(void);
extern "C" uintptr_t pc_crash_get_addr(void);

int fpcMtd_Method(process_method_func i_method, void* i_process) {
    if (i_method != NULL)
        return i_method(i_process);
    else
        return 1;
}

int fpcMtd_Execute(process_method_class* i_methods, void* i_process) {
    return fpcMtd_Method(i_methods->execute_method, i_process);
}

int fpcMtd_IsDelete(process_method_class* i_methods, void* i_process) {
    return fpcMtd_Method(i_methods->is_delete_method, i_process);
}

int fpcMtd_Delete(process_method_class* i_methods, void* i_process) {
    jmp_buf deleteBuf;
    jmp_buf* prevDeleteBuf = pc_crash_get_jmpbuf();
    pc_crash_set_jmpbuf(&deleteBuf);
    if (setjmp(deleteBuf) != 0) {
        pc_crash_set_jmpbuf(prevDeleteBuf);
        static int s_delete_crash_log = 0;
        if (s_delete_crash_log++ < 50) {
            fprintf(stderr, "[fpcMtd] delete crashed: proc=%p addr=0x%lx, forcing delete success\n",
                    i_process, (unsigned long)pc_crash_get_addr());
        }
        return 1;
    }
    int ret = fpcMtd_Method(i_methods->delete_method, i_process);
    pc_crash_set_jmpbuf(prevDeleteBuf);
    return ret;
}

int fpcMtd_Create(process_method_class* i_methods, void* i_process) {
    return fpcMtd_Method(i_methods->create_method, i_process);
}
