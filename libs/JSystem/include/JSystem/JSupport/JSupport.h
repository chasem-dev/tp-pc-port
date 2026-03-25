#ifndef JSUPPORT_H
#define JSUPPORT_H

#ifdef __REVOLUTION_SDK__
#include <revolution.h>
#else
#include <dolphin.h>
#endif
#include <stdint.h>

/**
* @ingroup jsystem-jsupport
* 
*/
template <typename T>
T* JSUConvertOffsetToPtr(const void* ptr, uintptr_t offset) {
#ifdef TARGET_PC
    /* On 64-bit, offsets in J3D binary data are 32-bit values.
     * Mask to 32 bits to avoid reading garbage from adjacent fields. */
    u32 off32 = (u32)offset;
    return off32 == 0 ? NULL : (T*)((uintptr_t)ptr + off32);
#else
    return offset == 0 ? NULL : (T*)((intptr_t)ptr + (intptr_t)offset);
#endif
}

/**
* @ingroup jsystem-jsupport
*
*/
template <typename T>
T* JSUConvertOffsetToPtr(const void* ptr, const void* offset) {
#ifdef TARGET_PC
    /* On 64-bit, void* reads 8 bytes from binary data that only has 4-byte offsets.
     * Extract only the lower 32 bits (the actual offset value after byte-swap). */
    u32 off32 = (u32)(uintptr_t)offset;
    return off32 == 0 ? NULL : (T*)((uintptr_t)ptr + off32);
#else
    T* ret;
    if (offset == 0) {
        ret = NULL;
    } else {
        ret = (T*)((intptr_t)ptr + (intptr_t)offset);
    }
    return ret;
#endif
}

inline u8 JSULoNibble(u8 param_0) { return param_0 & 0x0f; }
inline u8 JSUHiNibble(u8 param_0) {return param_0 >> 4; }

inline u8 JSULoByte(u16 in) {
    return in & 0xff;
}

inline u8 JSUHiByte(u16 in) {
    return in >> 8;
}

inline u16 JSUHiHalf(u32 in) {
    return (in >> 16);
}

inline u16 JSULoHalf(u32 param_0) {return param_0; }

#endif
