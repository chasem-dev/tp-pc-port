#ifndef F_PC_MANAGER_H_
#define F_PC_MANAGER_H_

#include "f_op/f_op_scene.h"
#include "f_pc/f_pc_create_iter.h"
#include "f_pc/f_pc_executor.h"
#include "f_pc/f_pc_leaf.h"
#include "f_pc/f_pc_node.h"
#include "f_pc/f_pc_stdcreate_req.h"
#include "f_pc/f_pc_searcher.h"
#include <type_traits>

enum {
    fpcM_UNK_PROCESS_ID_e = 0xFFFFFFFE,
    fpcM_ERROR_PROCESS_ID_e = 0xFFFFFFFF,
};

typedef int (*FastCreateReqFunc)(void*);
typedef void (*fpcM_ManagementFunc)(void);
typedef int (*fpcM_DrawIteraterFunc)(void*, void*);

inline fpc_ProcID fpcM_GetID(const base_process_class* i_process) {
    return i_process != NULL ? ((base_process_class*)i_process)->id : fpcM_ERROR_PROCESS_ID_e;
}

inline fpc_ProcID fpcM_GetID(const void* i_process) {
    return fpcM_GetID(reinterpret_cast<const base_process_class*>(i_process));
}

template <typename T, std::enable_if_t<std::is_convertible_v<const T*, const base_process_class*>, int> = 0>
inline fpc_ProcID fpcM_GetID(const T* i_process) {
    return fpcM_GetID(static_cast<const base_process_class*>(i_process));
}

inline s16 fpcM_GetName(const base_process_class* i_process) {
    return ((base_process_class*)i_process)->name;
}

inline s16 fpcM_GetName(const void* i_process) {
    return fpcM_GetName(reinterpret_cast<const base_process_class*>(i_process));
}

template <typename T, std::enable_if_t<std::is_convertible_v<const T*, const base_process_class*>, int> = 0>
inline s16 fpcM_GetName(const T* i_process) {
    return fpcM_GetName(static_cast<const base_process_class*>(i_process));
}

inline u32 fpcM_GetParam(const base_process_class* i_process) {
    return ((base_process_class*)i_process)->parameters;
}

inline u32 fpcM_GetParam(const void* i_process) {
    return fpcM_GetParam(reinterpret_cast<const base_process_class*>(i_process));
}

template <typename T, std::enable_if_t<std::is_convertible_v<const T*, const base_process_class*>, int> = 0>
inline u32 fpcM_GetParam(const T* i_process) {
    return fpcM_GetParam(static_cast<const base_process_class*>(i_process));
}

inline void fpcM_SetParam(base_process_class* i_process, u32 param) {
    ((base_process_class*)i_process)->parameters = param;
}

inline void fpcM_SetParam(void* i_process, u32 param) {
    fpcM_SetParam(reinterpret_cast<base_process_class*>(i_process), param);
}

template <typename T, std::enable_if_t<std::is_convertible_v<T*, base_process_class*>, int> = 0>
inline void fpcM_SetParam(T* i_process, u32 param) {
    fpcM_SetParam(static_cast<base_process_class*>(i_process), param);
}

inline s16 fpcM_GetProfName(const base_process_class* i_process) {
    return ((base_process_class*)i_process)->profname;
}

inline s16 fpcM_GetProfName(const void* i_process) {
    return fpcM_GetProfName(reinterpret_cast<const base_process_class*>(i_process));
}

template <typename T, std::enable_if_t<std::is_convertible_v<const T*, const base_process_class*>, int> = 0>
inline s16 fpcM_GetProfName(const T* i_process) {
    return fpcM_GetProfName(static_cast<const base_process_class*>(i_process));
}

inline fpc_ProcID fpcM_Create(s16 i_procName, FastCreateReqFunc i_createFunc, void* i_append) {
    return fpcSCtRq_Request(fpcLy_CurrentLayer(), i_procName, (stdCreateFunc)i_createFunc, NULL,
                            i_append);
}

inline int fpcM_DrawPriority(const void* i_process) {
    return fpcLf_GetPriority((const leafdraw_class*)i_process);
}

inline int fpcM_ChangeLayerID(base_process_class* i_process, int i_layerID) {
    return fpcPi_Change(&((base_process_class*)i_process)->priority, i_layerID, 0xFFFD, 0xFFFD);
}

inline int fpcM_ChangeLayerID(void* i_process, int i_layerID) {
    return fpcM_ChangeLayerID(reinterpret_cast<base_process_class*>(i_process), i_layerID);
}

template <typename T, std::enable_if_t<std::is_convertible_v<T*, base_process_class*>, int> = 0>
inline int fpcM_ChangeLayerID(T* i_process, int i_layerID) {
    return fpcM_ChangeLayerID(static_cast<base_process_class*>(i_process), i_layerID);
}

inline int fpcM_MakeOfType(int* i_type) {
    return fpcBs_MakeOfType(i_type);
}

inline BOOL fpcM_IsJustType(int i_typeA, int i_typeB) {
    return fpcBs_Is_JustOfType(i_typeA, i_typeB);
}

inline bool fpcM_IsFirstCreating(const base_process_class* i_process) {
    return ((base_process_class*)i_process)->state.init_state == 0;
}

inline bool fpcM_IsFirstCreating(void* i_process) {
    return fpcM_IsFirstCreating(reinterpret_cast<const base_process_class*>(i_process));
}

template <typename T, std::enable_if_t<std::is_convertible_v<const T*, const base_process_class*>, int> = 0>
inline bool fpcM_IsFirstCreating(const T* i_process) {
    return fpcM_IsFirstCreating(static_cast<const base_process_class*>(i_process));
}

inline process_profile_definition* fpcM_GetProfile(base_process_class* i_process) {
    return ((base_process_class*)i_process)->profile;
}

inline process_profile_definition* fpcM_GetProfile(void* i_process) {
    return fpcM_GetProfile(reinterpret_cast<base_process_class*>(i_process));
}

template <typename T, std::enable_if_t<std::is_convertible_v<T*, base_process_class*>, int> = 0>
inline process_profile_definition* fpcM_GetProfile(T* i_process) {
    return fpcM_GetProfile(static_cast<base_process_class*>(i_process));
}

inline void* fpcM_GetAppend(const base_process_class* i_process) {
    return ((base_process_class*)i_process)->append;
}

inline void* fpcM_GetAppend(const void* i_process) {
    return fpcM_GetAppend(reinterpret_cast<const base_process_class*>(i_process));
}

template <typename T, std::enable_if_t<std::is_convertible_v<const T*, const base_process_class*>, int> = 0>
inline void* fpcM_GetAppend(const T* i_process) {
    return fpcM_GetAppend(static_cast<const base_process_class*>(i_process));
}

inline BOOL fpcM_IsExecuting(fpc_ProcID id) {
    return fpcEx_IsExist(id);
}

inline void* fpcM_LyJudge(process_node_class* i_node, fpcLyIt_JudgeFunc i_func, void* i_data) {
    return fpcLyIt_Judge(&i_node->layer, i_func, i_data);
}

inline base_process_class* fpcM_Search(fpcLyIt_JudgeFunc i_func, void* i_data) {
    return fpcEx_Search(i_func, i_data);
}

inline base_process_class* fpcM_SearchByName(s16 name) {
    return (base_process_class*)fpcLyIt_AllJudge(fpcSch_JudgeForPName, &name);
}

inline base_process_class* fpcM_SearchByID(fpc_ProcID i_id) {
    return fpcEx_SearchByID(i_id);
}

inline process_node_class* fpcM_Layer(base_process_class* i_process) {
    return ((base_process_class*)i_process)->layer_tag.layer->process_node;
}

inline process_node_class* fpcM_Layer(void* i_process) {
    return fpcM_Layer(reinterpret_cast<base_process_class*>(i_process));
}

template <typename T, std::enable_if_t<std::is_convertible_v<T*, base_process_class*>, int> = 0>
inline process_node_class* fpcM_Layer(T* i_process) {
    return fpcM_Layer(static_cast<base_process_class*>(i_process));
}

void fpcM_Draw(void* i_process);
int fpcM_DrawIterater(fpcM_DrawIteraterFunc i_drawIterFunc);
int fpcM_Execute(void* i_process);
int fpcM_Delete(void* i_process);
BOOL fpcM_IsCreating(fpc_ProcID i_id);
void fpcM_Management(fpcM_ManagementFunc i_preExecuteFn, fpcM_ManagementFunc i_postExecuteFn);
void fpcM_Init();
base_process_class* fpcM_FastCreate(s16 i_procname, FastCreateReqFunc i_createReqFunc,
                                    void* i_createData, void* i_append);
int fpcM_IsPause(void* i_process, u8 i_flag);
void fpcM_PauseEnable(void* i_process, u8 i_flag);
void fpcM_PauseDisable(void* i_process, u8 i_flag);
void* fpcM_JudgeInLayer(fpc_ProcID i_layerID, fpcCtIt_JudgeFunc i_judgeFunc, void* i_data);

#endif
