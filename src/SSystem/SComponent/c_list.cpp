/**
 * c_list.cpp
 *
 */

#include "SSystem/SComponent/c_list.h"
#include "SSystem/SComponent/c_tag.h"
#include "SSystem/SComponent/c_node.h"
#if !PLATFORM_GCN
#include <dlfcn.h>
#include <stdio.h>
#endif
#include <types.h>

void cLs_Init(node_list_class* list) {
    list->mpHead = NULL;
    list->mpTail = NULL;
    list->mSize = 0;
}

int cLs_SingleCut(node_class* node) {
    node_list_class* list = (node_list_class*)node->mpData;

    if (node == list->mpHead)
        list->mpHead = node->mpNextNode;
    if (node == list->mpTail)
        list->mpTail = node->mpPrevNode;

    cNd_SingleCut(node);
    cNd_ClearObject(node);

    int ret;
    if (--list->mSize > 0) {
        ret = 1;
    } else {
        ret = 0;
    }

    return ret;
}

int cLs_Addition(node_list_class* list, node_class* node) {
#if !PLATFORM_GCN
    static unsigned int s_addCount = 0;
    if (s_addCount < 1024) {
        s_addCount++;
        Dl_info info0 = {};
        Dl_info info1 = {};
        void* ret0 = __builtin_return_address(0);
        void* ret1 = __builtin_return_address(1);
        dladdr(ret0, &info0);
        dladdr(ret1, &info1);
        fprintf(stderr,
                "[LIST] add #%u list=%p head=%p tail=%p node=%p tagData=%p size=%d ret0=%s ret1=%s\n",
                s_addCount,
                list,
                list != NULL ? list->mpHead : NULL,
                list != NULL ? list->mpTail : NULL,
                node,
                node != NULL ? ((create_tag_class*)node)->mpTagData : NULL,
                list != NULL ? list->mSize : -1,
                info0.dli_sname != NULL ? info0.dli_sname : "?",
                info1.dli_sname != NULL ? info1.dli_sname : "?");
        fflush(stderr);
    }
#endif
    if (list->mpTail == NULL) {
        list->mpHead = node;
    } else {
        cNd_Addition(list->mpTail, node);
    }

    list->mpTail = cNd_Last(node);
    cNd_SetObject(node, list);
    list->mSize = cNd_LengthOf(list->mpHead);
    return list->mSize;
}

int cLs_Insert(node_list_class* list, int idx, node_class* node) {
    node_class* pExisting = cNd_Order(list->mpHead, idx);
    if (pExisting == NULL) {
        return cLs_Addition(list, node);
    }

    cNd_SetObject(node, list);
    cNd_Insert(pExisting, node);
    list->mpHead = cNd_First(node);
    list->mSize = cNd_LengthOf(list->mpHead);
    return list->mSize;
}

node_class* cLs_GetFirst(node_list_class* list) {
    if (list->mSize != 0) {
        node_class* pHead = list->mpHead;
        cLs_SingleCut(pHead);
        return pHead;
    }

    return NULL;
}

void cLs_Create(node_list_class* list) {
    cLs_Init(list);
}
