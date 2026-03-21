/* pc_card.cpp - Memory card (CARD) API stubs */
#include "pc_platform.h"

extern "C" {

#define CARD_RESULT_READY       0
#define CARD_RESULT_BUSY       -1
#define CARD_RESULT_NOCARD     -3
#define CARD_RESULT_FATAL_ERROR -128

s32 CARDInit(void) { return CARD_RESULT_READY; }
s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize) {
    if (memSize) *memSize = 16 * 1024 * 1024; /* 16MB */
    if (sectorSize) *sectorSize = 8192;
    return CARD_RESULT_READY;
}
s32 CARDProbe(s32 chan) { (void)chan; return CARD_RESULT_READY; }
s32 CARDMount(s32 chan, void* workArea, void* detachCallback) {
    (void)chan; (void)workArea; (void)detachCallback;
    return CARD_RESULT_READY;
}
s32 CARDUnmount(s32 chan) { (void)chan; return CARD_RESULT_READY; }
s32 CARDCheck(s32 chan) { (void)chan; return CARD_RESULT_READY; }
s32 CARDCheckAsync(s32 chan, void* callback) { (void)chan; (void)callback; return CARD_RESULT_READY; }

s32 CARDCreate(s32 chan, const char* fileName, u32 size, void* fileInfo) {
    (void)chan; (void)fileName; (void)size; (void)fileInfo;
    return CARD_RESULT_READY;
}
s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, void* fileInfo, void* callback) {
    (void)chan; (void)fileName; (void)size; (void)fileInfo; (void)callback;
    return CARD_RESULT_READY;
}
s32 CARDOpen(s32 chan, const char* fileName, void* fileInfo) {
    (void)chan; (void)fileName; (void)fileInfo;
    return CARD_RESULT_NOCARD;
}
s32 CARDClose(void* fileInfo) { (void)fileInfo; return CARD_RESULT_READY; }

s32 CARDRead(void* fileInfo, void* addr, s32 length, s32 offset) {
    (void)fileInfo; (void)addr; (void)length; (void)offset;
    return CARD_RESULT_NOCARD;
}
s32 CARDReadAsync(void* fileInfo, void* addr, s32 length, s32 offset, void* callback) {
    (void)fileInfo; (void)addr; (void)length; (void)offset; (void)callback;
    return CARD_RESULT_NOCARD;
}
s32 CARDWrite(void* fileInfo, const void* addr, s32 length, s32 offset) {
    (void)fileInfo; (void)addr; (void)length; (void)offset;
    return CARD_RESULT_NOCARD;
}
s32 CARDWriteAsync(void* fileInfo, const void* addr, s32 length, s32 offset, void* callback) {
    (void)fileInfo; (void)addr; (void)length; (void)offset; (void)callback;
    return CARD_RESULT_NOCARD;
}
s32 CARDDelete(s32 chan, const char* fileName) { (void)chan; (void)fileName; return CARD_RESULT_NOCARD; }
s32 CARDDeleteAsync(s32 chan, const char* fileName, void* callback) {
    (void)chan; (void)fileName; (void)callback; return CARD_RESULT_NOCARD;
}

s32 CARDGetResultCode(s32 chan) { (void)chan; return CARD_RESULT_READY; }
s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed) {
    if (byteNotUsed) *byteNotUsed = 1024 * 1024;
    if (filesNotUsed) *filesNotUsed = 100;
    return CARD_RESULT_READY;
}
s32 CARDGetStatus(s32 chan, s32 fileNo, void* stat) {
    (void)chan; (void)fileNo; (void)stat;
    return CARD_RESULT_NOCARD;
}
s32 CARDSetStatus(s32 chan, s32 fileNo, void* stat) {
    (void)chan; (void)fileNo; (void)stat;
    return CARD_RESULT_NOCARD;
}
s32 CARDSetStatusAsync(s32 chan, s32 fileNo, void* stat, void* callback) {
    (void)chan; (void)fileNo; (void)stat; (void)callback;
    return CARD_RESULT_NOCARD;
}
s32 CARDGetEncoding(s32 chan, u16* encoding) {
    if (encoding) *encoding = 0;
    return CARD_RESULT_READY;
}
s32 CARDRename(s32 chan, const char* oldName, const char* newName) {
    (void)chan; (void)oldName; (void)newName;
    return CARD_RESULT_NOCARD;
}
s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName, void* callback) {
    (void)chan; (void)oldName; (void)newName; (void)callback;
    return CARD_RESULT_NOCARD;
}
s32 CARDFormat(s32 chan) { (void)chan; return CARD_RESULT_READY; }
s32 CARDFormatAsync(s32 chan, void* callback) { (void)chan; (void)callback; return CARD_RESULT_READY; }

u32 CARDGetXferredBytes(s32 chan) { (void)chan; return 0; }

s32 CARDGetSerialNo(s32 chan, u64* serialNo) {
    if (serialNo) *serialNo = 0x0123456789ABCDEFULL;
    return 0; /* CARD_RESULT_READY */
}

} /* extern "C" */
