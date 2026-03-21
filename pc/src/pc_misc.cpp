/* pc_misc.cpp - HW register arrays, PPC stubs, misc */
#include "pc_platform.h"

extern "C" {

/* --- HW register arrays --- */
volatile u16 __VIRegs[59]    = {0};
volatile u32 __PIRegs[12]    = {0};
volatile u16 __MEMRegs[64]   = {0};
volatile u16 __DSPRegs[32]   = {0};
volatile u32 __DIRegs[16]    = {0};
volatile u32 __SIRegs[0x100] = {0};
volatile u32 __EXIRegs[0x40] = {0};
volatile u32 __AIRegs[8]     = {0};

u32 __OSPhysicalMemSize = 24 * 1024 * 1024;
volatile int __OSTVMode = 0;
u32 __OSSimulatedMemSize = 24 * 1024 * 1024;
u32 __OSBusClock = 162000000;
u32 __OSCoreClock = 486000000;
volatile u16 __OSDeviceCode = 0;

/* --- EXI --- */
int EXILock(s32 chan, u32 dev, void* unlockCallback) { (void)chan; (void)dev; (void)unlockCallback; return 1; }
int EXIUnlock(s32 chan) { (void)chan; return 1; }
int EXISelect(s32 chan, u32 dev, u32 freq) { (void)chan; (void)dev; (void)freq; return 1; }
int EXIDeselect(s32 chan) { (void)chan; return 1; }
int EXIImm(s32 chan, void* data, s32 len, u32 type, void* callback) {
    (void)chan; (void)data; (void)len; (void)type; (void)callback; return 1;
}
int EXIDma(s32 chan, void* data, s32 len, u32 type, void* callback) {
    (void)chan; (void)data; (void)len; (void)type; (void)callback; return 1;
}
int EXISync(s32 chan) { (void)chan; return 1; }
void EXIInit(void) {}
int EXIAttach(s32 chan, void* extCallback) { (void)chan; (void)extCallback; return 1; }
int EXIDetach(s32 chan) { (void)chan; return 1; }
int EXIProbe(s32 chan) { (void)chan; return 0; }
int EXIProbeEx(s32 chan) { (void)chan; return 0; }
s32 EXIGetID(s32 chan, u32 dev, u32* id) { (void)chan; (void)dev; if (id) *id = 0; return 0; }
void EXISetExiCallback(s32 chan, void* cb) { (void)chan; (void)cb; }

/* --- SI --- */
void SIInit(void) {}
int SITransfer(s32 chan, void* output, u32 outputLen, void* input, u32 inputLen,
               void* callback, s64 time) {
    (void)chan; (void)output; (void)outputLen; (void)input; (void)inputLen;
    (void)callback; (void)time; return 1;
}
u32 SISetXY(u32 x, u32 y) { (void)x; (void)y; return 0; }
u32 SIEnablePolling(u32 poll) { (void)poll; return 0; }
u32 SIDisablePolling(u32 poll) { (void)poll; return 0; }
void SISetSamplingRate(u32 rate) { (void)rate; }
int SIIsChanBusy(s32 chan) { (void)chan; return 0; }
void SIRefreshSamplingRate(void) {}
int SIRegisterPollingHandler(void* handler) { (void)handler; return 1; }
int SIUnregisterPollingHandler(void* handler) { (void)handler; return 1; }

/* --- PPC --- */
u32 PPCMfmsr(void) { return 0; }
void PPCMtmsr(u32 msr) { (void)msr; }
u32 PPCMfhid0(void) { return 0; }
void PPCMthid0(u32 hid0) { (void)hid0; }
u32 PPCMfhid2(void) { return 0; }
void PPCMthid2(u32 hid2) { (void)hid2; }
void PPCHalt(void) { exit(1); }
u32 PPCMfl2cr(void) { return 0; }
void PPCMtl2cr(u32 val) { (void)val; }
void PPCMtdec(u32 val) { (void)val; }
void PPCSync(void) {}
void PPCMtmmcr0(u32 val) { (void)val; }
void PPCMtmmcr1(u32 val) { (void)val; }
void PPCMtpmc1(u32 val) { (void)val; }
void PPCMtpmc2(u32 val) { (void)val; }
void PPCMtpmc3(u32 val) { (void)val; }
void PPCMtpmc4(u32 val) { (void)val; }
u32 PPCMfpmc1(void) { return 0; }
u32 PPCMfpmc2(void) { return 0; }
u32 PPCMfpmc3(void) { return 0; }
u32 PPCMfpmc4(void) { return 0; }
u32 PPCMffpscr(void) { return 0; }
void PPCMtfpscr(u32 val) { (void)val; }

/* --- Debugger --- */
void DBInit(void) {}
int DBIsDebuggerPresent(void) { return 0; }
void DBPrintf(const char* fmt, ...) {
    if (!g_pc_verbose) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/* --- OS reset --- */
void OSRegisterResetFunction(void* info) { (void)info; }
void OSUnregisterResetFunction(void* info) { (void)info; }

/* --- OS modules (REL) --- */
void* __OSModuleList = NULL;
void* __OSStringTable = NULL;

int OSLink(void* info, void* bss) { (void)info; (void)bss; return 1; }
int OSLinkFixed(void* info, void* bss) { (void)info; (void)bss; return 1; }
int OSUnlink(void* info) { (void)info; return 1; }

/* --- GX mode tables --- */
u8 GXNtsc480IntDf[64] = {0};
u8 GXNtsc480Int[64] = {0};
u8 GXMpal480IntDf[64] = {0};
u8 GXPal528IntDf[64] = {0};
u8 GXEurgb60Hz480IntDf[64] = {0};

/* --- Misc --- */
void OSInitFastCast(void) {}
void __OSSetupFPU(void) {}
void __isync(void) {}

void InitMetroTRK(void) {}
void InitMetroTRK_BBA(void) {}

u16 OSGetFontEncode(void) { return 0; }
u32 OSGetProgressiveMode(void) { return 0; }
void OSSetProgressiveMode(u32 on) { (void)on; }
u32 OSGetSoundMode(void) { return 0; }
void OSSetSoundMode(u32 mode) { (void)mode; }
void OSProtectRange(u32 chan, void* addr, u32 nBytes, u32 control) {
    (void)chan; (void)addr; (void)nBytes; (void)control;
}
void* OSSetErrorHandler(u16 error, void* handler) { (void)error; (void)handler; return NULL; }

void __OSUnhandledException(u8 type, void* ctx, u32 dsisr, u32 dar) {
    (void)type; (void)ctx; (void)dsisr; (void)dar;
    fprintf(stderr, "Unhandled exception type %d\n", type);
}

/* perf counters */
void PERFInit(u32 timer_count, u32 event_count) { (void)timer_count; (void)event_count; }
void PERFStartAutoSampling(float samp_rate) { (void)samp_rate; }
void PERFStopAutoSampling(void) {}

/* THP video player stubs */
void THPPlayerInit(int mode) { (void)mode; }

} /* extern "C" */
