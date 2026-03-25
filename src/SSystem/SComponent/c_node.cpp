/**
 * c_node.cpp
 *
 */

#include "SSystem/SComponent/c_node.h"
#if !PLATFORM_GCN
#include <dlfcn.h>
#include <execinfo.h>
#include <stdio.h>
#include <string.h>
#endif
#include <types.h>

#if !PLATFORM_GCN
static bool cNd_isSuspiciousNodePtr(const node_class* node) {
    if (node == NULL) {
        return true;
    }

    Dl_info info;
    if (dladdr(node, &info) != 0 && info.dli_sname != NULL && strstr(info.dli_sname, "vtable") != NULL) {
        return true;
    }

    return false;
}

static void cNd_logSuspiciousJoin(const node_class* node_a, const node_class* node_b) {
    Dl_info info_a = {};
    Dl_info info_b = {};
    Dl_info caller0 = {};
    Dl_info caller1 = {};
    void* ret0 = __builtin_return_address(0);
    void* ret1 = __builtin_return_address(1);
    dladdr(node_a, &info_a);
    dladdr(node_b, &info_b);
    dladdr(ret0, &caller0);
    dladdr(ret1, &caller1);

    fprintf(stderr,
            "[NODE] suspicious join: a=%p symA=%s b=%p symB=%s ret0=%p (%s) ret1=%p (%s)\n",
            node_a,
            info_a.dli_sname != NULL ? info_a.dli_sname : "?",
            node_b,
            info_b.dli_sname != NULL ? info_b.dli_sname : "?",
            ret0,
            caller0.dli_sname != NULL ? caller0.dli_sname : "?",
            ret1,
            caller1.dli_sname != NULL ? caller1.dli_sname : "?");

    void* stack[16];
    const int stack_count = backtrace(stack, 16);
    backtrace_symbols_fd(stack, stack_count, fileno(stderr));
    fflush(stderr);
}

static void cNd_logJoinTrace(const node_class* node_a, const node_class* node_b) {
    static unsigned int s_joinCount = 0;
    if (s_joinCount >= 4096) {
        return;
    }
    s_joinCount++;

    Dl_info caller0 = {};
    Dl_info caller1 = {};
    void* ret0 = __builtin_return_address(0);
    void* ret1 = __builtin_return_address(1);
    dladdr(ret0, &caller0);
    dladdr(ret1, &caller1);

    fprintf(stderr,
            "[NODE] join #%u a=%p b=%p ret0=%s ret1=%s\n",
            s_joinCount,
            node_a,
            node_b,
            caller0.dli_sname != NULL ? caller0.dli_sname : "?",
            caller1.dli_sname != NULL ? caller1.dli_sname : "?");
}
#endif

void cNd_Join(node_class* node_a, node_class* node_b) {
#if !PLATFORM_GCN
    cNd_logJoinTrace(node_a, node_b);
    if (cNd_isSuspiciousNodePtr(node_a) || cNd_isSuspiciousNodePtr(node_b)) {
        cNd_logSuspiciousJoin(node_a, node_b);
    }
#endif
    node_a->mpNextNode = node_b;
    node_b->mpPrevNode = node_a;
}

int cNd_LengthOf(node_class* node) {
    int count = 0;
    while (node) {
        count++;
        node = NODE_GET_NEXT(node);
    }
    return count;
}

node_class* cNd_First(node_class* node) {
    node_class* ret = NULL;
    while (node) {
        ret = node;
        node = NODE_GET_PREV(node);
    }
    return ret;
}

node_class* cNd_Last(node_class* node) {
    node_class* ret = NULL;
    while (node) {
        ret = node;
        node = NODE_GET_NEXT(node);
    }
    return ret;
}

node_class* cNd_Order(node_class* node, int idx) {
    node_class* ret = NULL;
    int i = 0;
    while (i < idx && node) {
        ret = node;
        i++;
        node = NODE_GET_NEXT(node);
    }

    return i < idx ? ret : NULL;
}

void cNd_SingleCut(node_class* node) {
    node_class* prev = node->mpPrevNode;
    node_class* next = node->mpNextNode;

    if (prev)
        prev->mpNextNode = node->mpNextNode;
    if (next)
        next->mpPrevNode = node->mpPrevNode;
    node->mpPrevNode = NULL;
    node->mpNextNode = NULL;
}

void cNd_Cut(node_class* node) {
    if (node->mpPrevNode)
        node->mpPrevNode->mpNextNode = NULL;
    node->mpPrevNode = NULL;
}

void cNd_Addition(node_class* node_a, node_class* node_b) {
    node_class* pLast = cNd_Last(node_a);
    cNd_Join(pLast, node_b);
}

void cNd_Insert(node_class* node_a, node_class* node_b) {
    if (node_a->mpPrevNode == NULL) {
        cNd_Addition(node_b, node_a);
    } else {
        node_class* prev = node_a->mpPrevNode;
        cNd_Cut(node_a);
        cNd_Addition(prev, node_b);
        cNd_Addition(node_b, node_a);
    }
}

void cNd_SetObject(node_class* node, void* data) {
    while (node) {
        node->mpData = data;
        node = NODE_GET_NEXT(node);
    }
}

void cNd_ClearObject(node_class* node) {
    cNd_SetObject(node, NULL);
}

void cNd_ForcedClear(node_class* node) {
    node->mpPrevNode = NULL;
    node->mpNextNode = NULL;
    node->mpData = NULL;
}

void cNd_Create(node_class* node, void* data) {
    node->mpPrevNode = NULL;
    node->mpNextNode = NULL;
    node->mpData = data;
}
