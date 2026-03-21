#ifndef J3DFIFO_H
#define J3DFIFO_H

#include <gx.h>
#include <gd.h>

#ifdef TARGET_PC
/* On PC, redirect direct FIFO writes to our GXCmd/GXParam functions */
#define J3DFifo_WRITE_U8(v)  GXCmd1u8(v)
#define J3DFifo_WRITE_U16(v) GXCmd1u16(v)
#define J3DFifo_WRITE_U32(v) GXCmd1u32(v)
#else
#define J3DFifo_WRITE_U8(v)  (GXWGFifo.u8 = (v))
#define J3DFifo_WRITE_U16(v) (GXWGFifo.u16 = (v))
#define J3DFifo_WRITE_U32(v) (GXWGFifo.u32 = (v))
#endif

inline void J3DFifoLoadBPCmd(u32 regval) {
    GXCmd1u8(GX_LOAD_BP_REG);
    GXCmd1u32(regval);
}

inline void J3DFifoWriteXFCmdHdr(u16 addr, u8 len) {
    GXCmd1u8(GX_LOAD_XF_REG);
    GXCmd1u16(len - 1);
    GXCmd1u16(addr);
}

inline void J3DFifoLoadIndx(u8 cmd, u16 indx, u16 addr) {
    J3DFifo_WRITE_U8(cmd);
    J3DFifo_WRITE_U16(indx);
    J3DFifo_WRITE_U16(addr);
}

inline void J3DFifoWriteCPCmd(u8 cmd, u32 param) {
    J3DFifo_WRITE_U8(GX_LOAD_CP_REG);
    J3DFifo_WRITE_U8(cmd);
    J3DFifo_WRITE_U32(param);
}

inline void J3DFifoLoadCPCmd(u8 reg, u32 value) {
    GXCmd1u8(GX_LOAD_CP_REG);
    GXCmd1u8(reg);
    GXCmd1u32(value);
}

inline void J3DFifoWriteXFCmd(u16 cmd, u16 len) {
    J3DFifo_WRITE_U8(GX_LOAD_XF_REG);
    J3DFifo_WRITE_U16(len - 1);
    J3DFifo_WRITE_U16(cmd);
}

inline void J3DFifoLoadXFCmdHdr(u16 addr, u8 len) {
    GXCmd1u8(GX_LOAD_XF_REG);
    GXCmd1u16(len - 1);
    GXCmd1u16(addr);
}

inline void J3DFifoLoadPosMtxIndx(u16 index, u32 addr) {
    GXCmd1u8(GX_LOAD_INDX_A);
    GXCmd1u16(index);
    GXCmd1u16(((sizeof(Vec) - 1) << 12) | (u16)(addr * 4));
}

inline void J3DFifoLoadNrmMtxIndx3x3(u16 index, u32 addr) {
    GXCmd1u8(GX_LOAD_INDX_B);
    GXCmd1u16(index);
    GXCmd1u16(((9 - 1) << 12) | (u16)((addr * 3) + 0x400));
}

void J3DFifoLoadPosMtxImm(f32 (*)[4], u32);
void J3DFifoLoadNrmMtxImm(f32 (*)[4], u32);
void J3DFifoLoadNrmMtxImm3x3(f32 (*)[3], u32);
void J3DFifoLoadNrmMtxToTexMtx(f32 (*)[4], u32);
void J3DFifoLoadNrmMtxToTexMtx3x3(f32 (*)[3], u32);
void J3DFifoLoadTexCached(GXTexMapID, u32, GXTexCacheSize, u32, GXTexCacheSize);

#endif /* J3DFIFO_H */
