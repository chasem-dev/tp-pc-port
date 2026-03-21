/* pc_audio.cpp - Audio stubs (no-op initially) */
#include "pc_platform.h"

extern "C" {

/* --- AI (Audio Interface) --- */
void AIInit(u8* stack) { (void)stack; }
void AIInitDMA(void* callback, u32 samples) { (void)callback; (void)samples; }
void AIStartDMA(void) {}
void AIStopDMA(void) {}
u32 AIGetDMABytesLeft(void) { return 0; }
u32 AIGetDMAStartAddr(void) { return 0; }
u32 AIGetDMALength(void) { return 0; }
void AISetDSPSampleRate(u32 rate) { (void)rate; }
u32 AIGetDSPSampleRate(void) { return 32000; }
void AIRegisterDMACallback(void* callback) { (void)callback; }
u32 AIGetStreamSampleCount(void) { return 0; }
u32 AIGetStreamSampleRate(void) { return 32000; }
void AISetStreamPlayState(u32 state) { (void)state; }
u32 AIGetStreamPlayState(void) { return 0; }
void AISetStreamTrigger(u32 trigger) { (void)trigger; }
u32 AIGetStreamTrigger(void) { return 0; }
void AISetStreamVolLeft(u8 vol) { (void)vol; }
u8 AIGetStreamVolLeft(void) { return 0; }
void AISetStreamVolRight(u8 vol) { (void)vol; }
u8 AIGetStreamVolRight(void) { return 0; }
void AIResetStreamSampleCount(void) {}

/* --- AX (Audio Executor) --- */
void AXInit(void) {}
void AXInitEx(u32 mode) { (void)mode; }
void AXQuit(void) {}
void AXRegisterCallback(void* callback) { (void)callback; }
void* AXRegisterAuxACallback(void* callback, void* context) { (void)callback; (void)context; return NULL; }
void* AXRegisterAuxBCallback(void* callback, void* context) { (void)callback; (void)context; return NULL; }
void* AXRegisterAuxCCallback(void* callback, void* context) { (void)callback; (void)context; return NULL; }
u32 AXGetMaxVoices(void) { return 64; }
void AXSetMaxVoices(u32 max) { (void)max; }
void* AXAcquireVoice(u32 priority, void* callback, u32 userdata) {
    (void)priority; (void)callback; (void)userdata; return NULL;
}
void AXFreeVoice(void* voice) { (void)voice; }
void AXSetVoiceSrc(void* voice, void* src) { (void)voice; (void)src; }
void AXSetVoiceSrcType(void* voice, u32 type) { (void)voice; (void)type; }
void AXSetVoiceVe(void* voice, void* ve) { (void)voice; (void)ve; }
void AXSetVoiceMix(void* voice, void* mix) { (void)voice; (void)mix; }
void AXSetVoiceState(void* voice, u16 state) { (void)voice; (void)state; }
void AXSetVoiceType(void* voice, u16 type) { (void)voice; (void)type; }
void AXSetVoiceAdpcm(void* voice, void* adpcm) { (void)voice; (void)adpcm; }
void AXSetVoiceLoop(void* voice, u16 loop) { (void)voice; (void)loop; }
void AXSetVoiceLoopAddr(void* voice, u32 addr) { (void)voice; (void)addr; }
void AXSetVoiceEndAddr(void* voice, u32 addr) { (void)voice; (void)addr; }
void AXSetVoiceCurrentAddr(void* voice, u32 addr) { (void)voice; (void)addr; }
u32 AXGetVoiceCurrentAddr(void* voice) { (void)voice; return 0; }
void AXSetVoiceAddr(void* voice, void* addr) { (void)voice; (void)addr; }
int AXIsVoiceRunning(void* voice) { (void)voice; return 0; }
void AXSetVoiceSrcRatio(void* voice, float ratio) { (void)voice; (void)ratio; }
u32 AXGetAuxAReturnVolume(void) { return 0; }
void AXSetAuxAReturnVolume(u32 vol) { (void)vol; }
u32 AXGetAuxBReturnVolume(void) { return 0; }
void AXSetAuxBReturnVolume(u32 vol) { (void)vol; }
void AXSetVoiceItdOn(void* voice) { (void)voice; }
void AXGetVoiceOffsets(void* voice, void* offsets) { (void)voice; (void)offsets; }
void AXSetVoiceOffsets(void* voice, void* offsets) { (void)voice; (void)offsets; }
u32 AXGetMasterVolume(void) { return 0; }
void AXSetMasterVolume(u32 vol) { (void)vol; }
void AXSetMode(u32 mode) { (void)mode; }
u32 AXGetMode(void) { return 0; }
void AXSetCompressor(u32 comp) { (void)comp; }

/* --- DSP --- */
void DSPInit(void) {}
int DSPCheckInit(void) { return 1; }
void DSPReset(void) {}
void DSPHalt(void) {}
void DSPUnhalt(void) {}
u32 DSPGetDMAStatus(void) { return 0; }
void* DSPAddTask(void* task) { (void)task; return NULL; }

/* --- DTK (Disc Track) --- */
void DTKInit(void) {}

void pc_audio_shutdown(void) {
    /* Nothing to clean up yet */
}

/* DSP task management internals */
void* __DSP_boot_task = NULL;
void* __DSP_curr_task = NULL;
void __DSP_exec_task(void* task) { (void)task; }
void __DSP_insert_task(void* task) { (void)task; }
void* __DSP_first_task = NULL;
void __DSP_remove_task(void* task) { (void)task; }
void DSPAssertInt(void) {}
u32 DSPCheckMailFromDSP(void) { return 0; }
u32 DSPCheckMailToDSP(void) { return 0; }
u32 DSPReadMailFromDSP(void) { return 0; }
void DSPSendMailToDSP(u32 mail) { (void)mail; }

} /* extern "C" */
