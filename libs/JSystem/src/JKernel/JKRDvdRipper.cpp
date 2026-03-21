//
// JKRDvdRipper
//

#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/JKernel/JKRDvdRipper.h"
#include "JSystem/JKernel/JKRDvdFile.h"
#include "JSystem/JKernel/JKRDecomp.h"
#include "JSystem/JUtility/JUTException.h"
#include <cstring>
#include <os.h>
#include <vi.h>
#include <stdint.h>

static int JKRDecompressFromDVD(JKRDvdFile*, void*, u32, u32, u32, u32, u32*);
static int decompSZS_subroutine(u8*, u8*);
static u8* firstSrcData();
static u8* nextSrcData(u8*);

void* JKRDvdRipper::loadToMainRAM(char const* name, u8* dst, JKRExpandSwitch expandSwitch,
                                  u32 dstLength, JKRHeap* heap,
                                  JKRDvdRipper::EAllocDirection allocDirection, u32 offset,
                                  int* pCompression, u32* param_8) {
    JKRDvdFile file;
    if (!file.open(name)) {
        return NULL;
    }
    return JKRDvdToMainRam(&file, dst, expandSwitch, dstLength, heap, allocDirection, offset,
                         pCompression, param_8);
}

void* JKRDvdRipper::loadToMainRAM(s32 entryNumber, u8* dst, JKRExpandSwitch expandSwitch,
                                  u32 dstLength, JKRHeap* heap,
                                  JKRDvdRipper::EAllocDirection allocDirection, u32 offset,
                                  int* pCompression, u32* param_8) {
    JKRDvdFile file;
    if (!file.open(entryNumber)) {
        return NULL;
    }
    return JKRDvdToMainRam(&file, dst, expandSwitch, dstLength, heap, allocDirection, offset,
                         pCompression, param_8);
}

bool JKRDvdRipper::errorRetry = true;

void* JKRDvdRipper::loadToMainRAM(JKRDvdFile* dvdFile, u8* dst, JKRExpandSwitch expandSwitch,
                                  u32 dstLength, JKRHeap* heap,
                                  JKRDvdRipper::EAllocDirection allocDirection, u32 offset,
                                  int* pCompression, u32* param_8) {
    s32 fileSizeAligned;
    bool hasAllocated = false;
    JKRCompression compression = COMPRESSION_NONE;
    u32 expandSize;
    u8 *mem = NULL;

#ifdef TARGET_PC
    if (0) fprintf(stderr, "[PC] loadToMainRAM: fileSize=%d\n", dvdFile->getFileSize());
#endif
    fileSizeAligned = ALIGN_NEXT(dvdFile->getFileSize(), 32);
    if (expandSwitch == EXPAND_SWITCH_UNKNOWN1)
    {
        u8 buffer[0x40];
        u8 *bufPtr = (u8 *)ALIGN_NEXT((uintptr_t)buffer, 32);
        while (true)
        {
            int readBytes = DVDReadPrio(dvdFile->getFileInfo(), bufPtr, 0x20, 0, 2);
            if (readBytes >= 0)
                break;

            if (readBytes == -3 || errorRetry == false)
                return NULL;

            VIWaitForRetrace();
        }
        DCInvalidateRange(bufPtr, 0x20);

        compression = JKRCheckCompressed_noASR(bufPtr);
        expandSize = JKRDecompExpandSize(bufPtr);
#ifdef TARGET_PC
        if (0) fprintf(stderr, "[PC] JKRDvdToMainRam: compression=%d expandSize=%u fileSize=%u\n",
                compression, expandSize, fileSizeAligned);
#endif
    }

    if (pCompression)
        *pCompression = (int)compression;

    if (expandSwitch == EXPAND_SWITCH_UNKNOWN1 && compression != COMPRESSION_NONE)
    {
        if (dstLength != 0 && expandSize > dstLength)
        {
            expandSize = dstLength;
        }
        if (dst == NULL)
        {
            dst = (u8 *)JKRAllocFromHeap(heap, expandSize, allocDirection == ALLOC_DIRECTION_FORWARD ? 32 : -32);
            hasAllocated = true;
        }
        if (dst == NULL)
            return NULL;
        if (compression == COMPRESSION_YAY0)
        {
            mem = (u8 *)JKRAllocFromHeap((heap), fileSizeAligned, 32);
            if (mem == NULL)
            {
                if (hasAllocated == true)
                {
                    JKRFree(dst);
                    return NULL;
                }
            }
        }
    }
    else
    {
        if (dst == NULL)
        {
            u32 size = fileSizeAligned - offset;
            if ((dstLength != 0) && (size > dstLength))
                size = dstLength;

            dst = (u8 *)JKRAllocFromHeap(heap, size, allocDirection == ALLOC_DIRECTION_FORWARD ? 32 : -32);
            hasAllocated = true;
        }
        if (dst == NULL)
            return NULL;
    }
    if (compression == COMPRESSION_NONE)
    {
        JKRCompression compression2 = COMPRESSION_NONE; // maybe for a sub archive?

        if (offset != 0)
        {
            u8 buffer[0x40];
            u8 *bufPtr = (u8 *)ALIGN_NEXT((uintptr_t)buffer, 32);
            while (true)
            {
                int readBytes = DVDReadPrio(dvdFile->getFileInfo(), bufPtr, 32, (s32)offset, 2);
                if (readBytes >= 0)
                    break;

                if (readBytes == -3 || !errorRetry)
                {
                    if (hasAllocated == true)
                    {
                        JKRFree(dst);
                    }
                    return NULL;
                }
                VIWaitForRetrace();
            }
            DCInvalidateRange(bufPtr, 32);

            compression2 = JKRCheckCompressed_noASR(bufPtr);
        }
        if ((compression2 == COMPRESSION_NONE || expandSwitch == EXPAND_SWITCH_UNKNOWN2) || expandSwitch == EXPAND_SWITCH_UNKNOWN0)
        {
            s32 size = fileSizeAligned - offset;
            if (dstLength != 0 && dstLength < size)
                size = dstLength; // probably a ternary
            while (true)
            {
                int readBytes = DVDReadPrio(dvdFile->getFileInfo(), dst, size, (s32)offset, 2);
                if (readBytes >= 0)
                    break;

                if (readBytes == -3 || !errorRetry)
                {
                    if (hasAllocated == true)
                        JKRFree(dst);
                    return NULL;
                }
                VIWaitForRetrace();
            }
            if (param_8)
            {
                *param_8 = size;
            }
            return dst;
        }
        else if (compression2 == COMPRESSION_YAZ0)
        {
#ifdef TARGET_PC
            /* Simple Yaz0 decode for sub-archive at offset */
            {
                u32 compSize = fileSizeAligned - offset;
                u8* compBuf2 = (u8*)malloc(compSize);
                DVDReadPrio(dvdFile->getFileInfo(), compBuf2, compSize, (s32)offset, 2);
                u32 expSz2 = ((u32)compBuf2[4]<<24)|((u32)compBuf2[5]<<16)|((u32)compBuf2[6]<<8)|compBuf2[7];
                if (dstLength != 0 && expSz2 > dstLength) expSz2 = dstLength;
                u8* src2 = compBuf2 + 0x10;
                u8* end2 = dst + expSz2;
                u8* dp2 = dst;
                int bits2 = 0, code2 = 0;
                while (dp2 < end2) {
                    if (bits2 == 0) { code2 = *src2++; bits2 = 8; }
                    if (code2 & 0x80) { *dp2++ = *src2++; }
                    else {
                        int b1 = *src2++, b2 = *src2++;
                        int dist = ((b1 & 0xF) << 8) | b2;
                        int n = (b1 >> 4) ? (b1 >> 4) + 2 : (*src2++) + 0x12;
                        u8* cp = dp2 - dist - 1;
                        for (int i = 0; i < n && dp2 < end2; i++) *dp2++ = *cp++;
                    }
                    code2 <<= 1; bits2--;
                }
                if (param_8) *param_8 = (u32)(dp2 - dst);
                free(compBuf2);
            }
#else
            JKRDecompressFromDVD(dvdFile, dst, fileSizeAligned, dstLength, 0, offset, param_8);
#endif
        } else {
            JUTException::panic(__FILE__, 0x143, "Sorry, not applied for SZP archive.");
        }
        return dst;
    }
    else if (compression == COMPRESSION_YAY0)
    {
        // SZP decompression
        // s32 readoffset = startOffset;
        if (offset != 0)
        {
            JUTException::panic(__FILE__, 0x14d, "Not support SZP with offset read");
        }
        while (true)
        {
            int readBytes = DVDReadPrio(dvdFile->getFileInfo(), mem, fileSizeAligned, 0, 2);
            if (readBytes >= 0)
                break;

            if (readBytes == -3 || !errorRetry)
            {
                if (hasAllocated == true)
                    JKRFree(dst);

                JKRFree(mem);
                return NULL;
            }
            VIWaitForRetrace();
        }
        DCInvalidateRange(mem, fileSizeAligned);
        JKRDecompress(mem, dst, expandSize, offset);
        JKRFree(mem);
        if (param_8)
        {
            *param_8 = expandSize;
        }
        return dst;
    }
    else if (compression == COMPRESSION_YAZ0)
    {
#ifdef TARGET_PC
        /* On PC, use simple read-all-then-decompress instead of streaming
         * decompression which has buffer management issues on 64-bit. */
        if (0) fprintf(stderr, "[PC] YAZ0 simple: fileSizeAligned=%u expandSize=%u offset=%u\n",
                fileSizeAligned, expandSize, offset);
        u8* compBuf = (u8*)malloc(fileSizeAligned);
        s32 readResult;
        while (true) {
            readResult = DVDReadPrio(dvdFile->getFileInfo(), compBuf, fileSizeAligned, 0, 2);
            if (readResult >= 0) break;
            if (readResult == -3 || !errorRetry) { free(compBuf); if (hasAllocated) JKRFree(dst); return NULL; }
        }
        if (0) fprintf(stderr, "[PC] YAZ0 compBuf: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
                compBuf[0], compBuf[1], compBuf[2], compBuf[3],
                compBuf[4], compBuf[5], compBuf[6], compBuf[7],
                compBuf[8], compBuf[9], compBuf[10], compBuf[11],
                compBuf[12], compBuf[13], compBuf[14], compBuf[15]);
        /* Decode Yaz0 in-memory — read expanded size from Yaz0 header directly
         * because the expandSize variable may have been capped by dstLength. */
        {
            u32 yaz0Size = ((u32)compBuf[4]<<24)|((u32)compBuf[5]<<16)|((u32)compBuf[6]<<8)|compBuf[7];
            /* If dst was allocated too small (expandSize was capped by dstLength), reallocate.
             * But only if the yaz0 header size looks sane (< 64MB) and we allocated the buffer. */
            if (yaz0Size > expandSize && yaz0Size < 0x4000000 && hasAllocated && dstLength == 0) {
                JKRFree(dst);
                dst = (u8*)JKRAllocFromHeap(heap, yaz0Size, allocDirection == ALLOC_DIRECTION_FORWARD ? 32 : -32);
                if (!dst) { free(compBuf); return NULL; }
                expandSize = yaz0Size;
            } else if (expandSize > 0 && expandSize < yaz0Size) {
                yaz0Size = expandSize; /* respect dstLength cap or caller-provided dst buffer */
            }
            u8* src = compBuf + 0x10;
            u8* end = dst + yaz0Size;
            u8* dp = dst;
            int bits = 0, code = 0;
            while (dp < end) {
                if (bits == 0) { code = *src++; bits = 8; }
                if (code & 0x80) { *dp++ = *src++; }
                else {
                    int b1 = *src++, b2 = *src++;
                    int dist = ((b1 & 0xF) << 8) | b2;
                    int n = (b1 >> 4) ? (b1 >> 4) + 2 : (*src++) + 0x12;
                    u8* cp = dp - dist - 1;
                    for (int i = 0; i < n && dp < end; i++) *dp++ = *cp++;
                }
                code <<= 1; bits--;
            }
            if (param_8) *param_8 = (u32)(dp - dst);
        }
        free(compBuf);
        u32 result = 0;
#else
        u32 result = JKRDecompressFromDVD(dvdFile, dst, fileSizeAligned, expandSize, offset, 0, param_8);
#endif
#ifdef TARGET_PC
        {
            /* Check non-zero bytes in decompressed output */
            int nz = 0;
            for (u32 i = 0; i < expandSize && i < 306176; i++) if (dst[i]) nz++;
            if (0) fprintf(stderr, "[PC] JKRDvdToMainRam YAZ0: dst=%p expandSize=%u tsPtr=%u nonzero=%d dst[32..35]=%02x%02x%02x%02x dst[544..547]=%02x%02x%02x%02x\n",
                    dst, expandSize, param_8 ? *param_8 : 0, nz,
                    dst[32], dst[33], dst[34], dst[35],
                    expandSize > 547 ? dst[544] : 0, expandSize > 547 ? dst[545] : 0,
                    expandSize > 547 ? dst[546] : 0, expandSize > 547 ? dst[547] : 0);
        }
#endif
        if (result != 0u)
        {
            if (hasAllocated)
                JKRFree(dst);
            dst = NULL;
        }
        return dst;
    }
    else if (hasAllocated)
    {
        JKRFree(dst);
        dst = NULL;
    }
    return NULL;
}

static u8 lit_491[12];

JSUList<JKRDMCommand> JKRDvdRipper::sDvdAsyncList;

static OSMutex decompMutex;

u32 JKRDvdRipper::sSZSBufferSize = 0x00000400;

static u8* szpBuf;

static u8* szpEnd;

static u8* refBuf;

static u8* refEnd;

static u8* refCurrent;

static u32 srcOffset;

static u32 transLeft;

static u8* srcLimit;

static JKRDvdFile* srcFile;

static u32 fileOffset;

static u32 readCount;

static u32 maxDest;

static bool data_80451458;

static u32* tsPtr;

static u32 tsArea;

static int JKRDecompressFromDVD(JKRDvdFile* dvdFile, void* dst, u32 fileSize, u32 inMaxDest,
                                u32 inFileOffset, u32 inSrcOffset, u32* inTsPtr) {
    BOOL interrupts = OSDisableInterrupts();
    if (data_80451458 == false)
    {
        OSInitMutex(&decompMutex);
        data_80451458 = true;
    }
    OSRestoreInterrupts(interrupts);
    OSLockMutex(&decompMutex);
    u32 result = 0;
    u32 szsBufferSize = JKRDvdRipper::getSZSBufferSize();
    szpBuf = (u8 *)JKRAllocFromSysHeap(szsBufferSize, -0x20);
    JUT_ASSERT(909, szpBuf != NULL);

    szpEnd = szpBuf + szsBufferSize;
    if (inFileOffset != 0) {
        refBuf = (u8 *)JKRAllocFromSysHeap(0x1120, -4);
        JUT_ASSERT(918, refBuf != NULL);
        refEnd = refBuf + 0x1120;
        refCurrent = refBuf;
    } else {
        refBuf = NULL;
    }
    srcFile = dvdFile;
    srcOffset = inSrcOffset;
    transLeft = fileSize - srcOffset;
    fileOffset = inFileOffset;
    readCount = 0;
    maxDest = inMaxDest;
    tsPtr = inTsPtr ? inTsPtr : &tsArea;
    *tsPtr = 0;
    u8 *data = firstSrcData();
    if (data != NULL) {
        result = decompSZS_subroutine(data, (u8 *)dst);
    } else {
        result = -1;
    }
    JKRFree(szpBuf);
    if (refBuf)
    {
        JKRFree(refBuf);
    }
    DCStoreRangeNoSync(dst, *tsPtr);
    OSUnlockMutex(&decompMutex);
    return result;
}

int decompSZS_subroutine(u8* src, u8* dest) {
    u8* endPtr;
    s32 validBitCount = 0;
    s32 currCodeByte = 0;
    u32 ts = 0;

    if (src[0] != 'Y' || src[1] != 'a' || src[2] != 'z' || src[3] != '0') {
        return -1;
    }

    SYaz0Header* header = (SYaz0Header*)src;
#ifdef TARGET_PC
    u32 expandedSize = (src[4] << 24) | (src[5] << 16) | (src[6] << 8) | src[7];
#else
    u32 expandedSize = header->length;
#endif
    endPtr = dest + (expandedSize - fileOffset);
    if (maxDest != 0 && endPtr > dest + maxDest) {
        endPtr = dest + maxDest;
    }

    src += 0x10;
    s32 b1;
    u32 dist;
    s32 numBytes;
    u8* copySource;
    do {
        if (validBitCount == 0) {
            if ((src > srcLimit) && transLeft) {
                src = nextSrcData(src);
                if (!src) {
                    return -1;
                }
            }
            currCodeByte = *src++;
            validBitCount = 8;
        }
        if (currCodeByte & 0x80) {
            if (fileOffset != 0) {
                if (readCount >= fileOffset) {
                    *dest = *src;
                    dest++;
                    ts++;
                    if (dest == endPtr) {
                        break;
                    }
                }
                *(refCurrent++) = *src;
                if (refCurrent == refEnd) {
                    refCurrent = refBuf;
                }
                src++;
            } else {
                *dest++ = *src++;
                ts++;
                if (dest == endPtr) {
                    break;
                }
            }
            readCount++;
        } else {
            b1 = src[0];
            dist = src[1] | ((b1 & 0x0f) << 8);
            numBytes = b1 >> 4;
            src += 2;
            if (fileOffset != 0) {
                copySource = refCurrent - dist - 1;
                if (copySource < refBuf) {
                    copySource += refEnd - refBuf;
                }
            } else {
                copySource = dest - dist - 1;
            }
            if (numBytes == 0) {
                numBytes = (*src++) + 0x12;
            } else {
                numBytes += 2;
            }
            if (fileOffset != 0) {
                do {
                    if (readCount >= fileOffset) {
                        *dest = *copySource;
                        dest++;
                        ts++;
                        if (dest == endPtr) {
                            break;
                        }
                    }
                    *(refCurrent++) = *copySource;
                    if (refCurrent == refEnd) {
                        refCurrent = refBuf;
                    }
                    copySource++;
                    if (copySource == refEnd) {
                        copySource = refBuf;
                    }
                    readCount++;
                    numBytes--;
                } while (numBytes != 0);
            } else {
                do {
                    *dest = *copySource;
                    dest++;
                    ts++;
                    if (dest == endPtr) {
                        break;
                    }
                    copySource++;
                    readCount++;
                    numBytes--;
                } while (numBytes != 0);
            }
        }
        currCodeByte <<= 1;
        validBitCount--;
    } while (dest < endPtr);
    *tsPtr = ts;
#ifdef TARGET_PC
    fprintf(stderr, "[PC] decompSZS: decompressed %u bytes (endPtr-dest_start=%td)\n",
            ts, (ptrdiff_t)(endPtr - (dest - ts)));
#endif
    return 0;
}

static u8* firstSrcData() {
    srcLimit = szpEnd - 0x19;
    u8* buffer = szpBuf;
    u32 bufSize = szpEnd - buffer;
    u32 length = transLeft < bufSize ? transLeft : bufSize;

    while (true) {
        int result = DVDReadPrio(srcFile->getFileInfo(), buffer, length, srcOffset, 2);
        if (result >= 0) {
            break;
        }

        if (result == -3 || !JKRDvdRipper::isErrorRetry()) {
            return NULL;
        }
        VIWaitForRetrace();
    }

    DCInvalidateRange(buffer, length);
    srcOffset += length;
    transLeft -= length;
    return buffer;
}

static u8* nextSrcData(u8* src) {
    u32 limit = szpEnd - src;
    u8 *dest;
    if (IS_NOT_ALIGNED(limit, 0x20)) {
        dest = szpBuf + 0x20 - (limit & (0x20 - 1));
    } else {
        dest = szpBuf;
    }

    memcpy(dest, src, limit);
    u32 transSize = (uintptr_t)(szpEnd - (dest + limit));
    if (transSize > transLeft) {
        transSize = transLeft;
    }
    JUT_ASSERT(1208, transSize > 0);

    while (true)
    {
        s32 result = DVDReadPrio(srcFile->getFileInfo(), (dest + limit), transSize, srcOffset, 2);
        if (result >= 0)
            break;
        // bug: supposed to call isErrorRetry, but didn't
        if (result == -3 || !JKRDvdRipper::isErrorRetry)
            return NULL;

        VIWaitForRetrace();
    }
    DCInvalidateRange((dest + limit), transSize);
    srcOffset += transSize;
    transLeft -= transSize;
    if (transLeft == 0)
        srcLimit = transSize + (dest + limit);

    return dest;
}
