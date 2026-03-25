/* pc_j3d_bswap.h - Byte-swap J3D binary data from GC big-endian to PC little-endian */
#ifndef PC_J3D_BSWAP_H
#define PC_J3D_BSWAP_H

#ifdef TARGET_PC

#include "pc_bswap.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

static inline bool pc_j3d_is_host_magic(uint32_t magic) {
    return magic == 'J3D1' || magic == 'J3D2';
}

static inline bool pc_j3d_needs_bswap(const void* data) {
    if (data == NULL) {
        return false;
    }

    uint32_t magic = *(const uint32_t*)data;
    if (pc_j3d_is_host_magic(magic)) {
        return false;
    }

    return pc_j3d_is_host_magic(pc_bswap32(magic));
}

static inline uint32_t pc_j3d_get_file_size(const void* data) {
    if (data == NULL) {
        return 0;
    }

    uint32_t raw_size = ((const uint32_t*)data)[2];
    return pc_j3d_needs_bswap(data) ? pc_bswap32(raw_size) : raw_size;
}

/* Choose a safe swap range for J3D resources.
 * - If resource_size_limit is provided, clamp header size to that entry size.
 * - If no limit is provided, keep the legacy conservative cap.
 * Returns 0 when the size is not trustworthy. */
static inline uint32_t pc_j3d_get_safe_swap_size(const void* data, uint32_t resource_size_limit,
                                                 uint32_t* out_header_size, const char** out_reason) {
    if (out_header_size) {
        *out_header_size = 0;
    }
    if (out_reason) {
        *out_reason = "ok";
    }
    if (data == NULL) {
        if (out_reason) *out_reason = "null-data";
        return 0;
    }

    uint32_t header_size = pc_j3d_get_file_size(data);
    if (out_header_size) {
        *out_header_size = header_size;
    }

    if (header_size < 0x20) {
        if (out_reason) *out_reason = "header-too-small";
        return 0;
    }

    if (resource_size_limit != 0) {
        if (resource_size_limit < 0x20) {
            if (out_reason) *out_reason = "entry-too-small";
            return 0;
        }

        if (header_size > resource_size_limit) {
            if (out_reason) *out_reason = "clamped-to-entry";
            return resource_size_limit;
        }
        return header_size;
    }

    if (header_size > 0x400000) {
        if (out_reason) *out_reason = "header-too-large-unbounded";
        return 0;
    }

    return header_size;
}

static inline bool pc_j3d_is_hierarchy_type(uint16_t type) {
    switch (type) {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x10:
    case 0x11:
    case 0x12:
        return true;
    default:
        return false;
    }
}

static inline bool pc_j3d_is_nonzero_hierarchy_type(uint16_t type) {
    switch (type) {
    case 0x01:
    case 0x02:
    case 0x10:
    case 0x11:
    case 0x12:
        return true;
    default:
        return false;
    }
}

static inline void pc_j3d_normalize_inf1_hierarchy(uint8_t* hier, uint32_t blockSize,
                                                    uint32_t hierOff) {
    int typeFirstScore = 0;
    int valueFirstScore = 0;
    uint8_t* p = hier;
    uint8_t* end = hier + (blockSize - hierOff);

    for (int i = 0; p + 4 <= end && i < 64; i++, p += 4) {
        uint16_t first = *(uint16_t*)(p + 0);
        uint16_t second = *(uint16_t*)(p + 2);
        if (first == 0 && second == 0) {
            break;
        }

        bool firstIsType = pc_j3d_is_nonzero_hierarchy_type(first);
        bool secondIsType = pc_j3d_is_nonzero_hierarchy_type(second);
        if (firstIsType && !secondIsType) {
            typeFirstScore++;
        } else if (secondIsType && !firstIsType) {
            valueFirstScore++;
        }
    }

    if (typeFirstScore <= valueFirstScore) {
        return;
    }

    for (p = hier; p + 4 <= end; p += 4) {
        uint16_t first = *(uint16_t*)(p + 0);
        uint16_t second = *(uint16_t*)(p + 2);
        *(uint16_t*)(p + 0) = second;
        *(uint16_t*)(p + 2) = first;
        if (first == 0 && second == 0) {
            break;
        }
    }

    fprintf(stderr,
            "[J3D] normalized INF1 hierarchy order at offset 0x%x"
            " (typeFirstScore=%d valueFirstScore=%d)\n",
            hierOff, typeFirstScore, valueFirstScore);
    fflush(stderr);
}

/* Swap a f32 in-place (same as u32 swap) */
static inline void pc_bswap_f32(void* p) {
    uint32_t* u = (uint32_t*)p;
    *u = pc_bswap32(*u);
}

static inline int pc_j3d_vcd_attr_plausible(uint32_t attr) {
    return (attr == 0xFF) || (attr <= 0x1A);
}

static inline int pc_j3d_vcd_type_plausible(uint32_t type) {
    return type <= 3;
}

static inline int pc_j3d_block_looks_native(uint32_t blockType, const uint8_t* d, uint32_t blockSize) {
    if (d == NULL || blockSize < 0x10) {
        return 0;
    }
    switch (blockType) {
    case 0x494E4631: { /* INF1 */
        /* mFlags at 0x08 (u16) should be small (0x0000-0x001F).
         * Hierarchy count at 0x0A is not directly stored, but the offset at 0x0C
         * should be a valid offset within the block. */
        uint16_t mFlags = *(const uint16_t*)(d + 0x08);
        uint32_t hierOff = *(const uint32_t*)(d + 0x0C);
        return mFlags <= 0x001F && hierOff > 0 && hierOff < blockSize;
    }
    case 0x56545831: { /* VTX1 */
        uint32_t off = *(const uint32_t*)(d + 0x08);
        return off < blockSize;
    }
    case 0x45565031: { /* EVP1 */
        uint16_t count = *(const uint16_t*)(d + 0x08);
        return count < 0x4000;
    }
    case 0x44525731: { /* DRW1 */
        uint16_t mtxNum = *(const uint16_t*)(d + 0x08);
        uint32_t flagOff = *(const uint32_t*)(d + 0x0C);
        uint32_t idxOff = *(const uint32_t*)(d + 0x10);
        return mtxNum < 0x4000 && flagOff < blockSize && idxOff < blockSize;
    }
    case 0x4A4E5431: { /* JNT1 */
        uint16_t jntNum = *(const uint16_t*)(d + 0x08);
        uint32_t jntOff = *(const uint32_t*)(d + 0x0C);
        uint32_t idxOff = *(const uint32_t*)(d + 0x10);
        return jntNum > 0 && jntNum < 0x400 && jntOff < blockSize && idxOff < blockSize;
    }
    case 0x53485031: { /* SHP1 */
        uint16_t shpNum = *(const uint16_t*)(d + 0x08);
        uint32_t shpOff = *(const uint32_t*)(d + 0x0C);
        uint32_t idxOff = *(const uint32_t*)(d + 0x10);
        uint32_t vcdOff = *(const uint32_t*)(d + 0x18);
        return shpNum > 0 && shpNum < 0x400 && shpOff < blockSize && idxOff < blockSize && vcdOff < blockSize;
    }
    case 0x4D415433: /* MAT3 */
    case 0x4D415432: { /* MAT2 */
        uint16_t matNum = *(const uint16_t*)(d + 0x08);
        uint32_t initOff = *(const uint32_t*)(d + 0x0C);
        return matNum > 0 && matNum < 0x400 && initOff < blockSize;
    }
    case 0x54455831: { /* TEX1 */
        uint16_t texNum = *(const uint16_t*)(d + 0x08);
        uint32_t texOff = *(const uint32_t*)(d + 0x0C);
        return texNum > 0 && texNum < 0x400 && texOff < blockSize;
    }
    default:
        /* For unknown block types, assume native if all other blocks tested native.
         * This is handled by the caller checking a known block first. */
        return 0;
    }
}

static inline void pc_j3d_unswap_words(uint8_t* data, uint32_t bytes) {
    if (data == NULL || bytes < 4) {
        return;
    }
    uint32_t words = bytes / 4;
    for (uint32_t i = 0; i < words; i++) {
        uint32_t* w = (uint32_t*)(data + i * 4);
        *w = pc_bswap32(*w);
    }
}

/* Some MAT3 payloads arrive with headers/offsets already native but table payload
 * still word-swapped in 32-bit chunks. Repair only when the table bytes strongly
 * match that pattern. */
static inline void pc_j3d_fix_mat3_wordswapped_tables(uint8_t* d, uint32_t blockSize) {
    if (d == NULL || blockSize < 0x84) {
        return;
    }

    uint32_t offsets[30];
    for (int k = 0; k < 30; k++) {
        offsets[k] = *(uint32_t*)(d + 0x0C + k * 4);
    }

    auto tableBytes = [&](int idx) -> uint32_t {
        uint32_t off = offsets[idx];
        if (off == 0 || off >= blockSize) {
            return 0;
        }
        uint32_t nextOff = blockSize;
        for (int i = 0; i < 30; i++) {
            if (offsets[i] > off && offsets[i] < nextOff) {
                nextOff = offsets[i];
            }
        }
        return (nextOff > off) ? (nextOff - off) : 0;
    };

    int suspicious = 0;
    const int probeIdx[3] = {6, 10, 19}; /* ColorChanNum, TexGenNum, TevStageNum */
    for (int i = 0; i < 3; i++) {
        uint32_t off = offsets[probeIdx[i]];
        uint32_t bytes = tableBytes(probeIdx[i]);
        if (off == 0 || off + 4 > blockSize || bytes < 4) {
            continue;
        }
        uint8_t b0 = d[off + 0];
        uint8_t b3 = d[off + 3];
        if (b0 > 0x20 && (b3 <= 0x10 || b3 == 0xFF)) {
            suspicious++;
        }
    }

    if (suspicious < 2) {
        return;
    }

    for (int i = 0; i < 30; i++) {
        uint32_t off = offsets[i];
        uint32_t bytes = tableBytes(i);
        if (off == 0 || off >= blockSize || bytes < 4) {
            continue;
        }
        pc_j3d_unswap_words(d + off, bytes);
    }
}

/* Swap a Vec (3x f32) in-place */
static inline void pc_bswap_vec3(void* p) {
    pc_bswap32_array(p, 3);
}

/* Swap a S16Vec (3x s16) in-place */
static inline void pc_bswap_s16vec3(void* p) {
    pc_bswap16_array(p, 3);
}

/* Byte-swap a ResTIMG (texture header, 0x20 bytes) in-place */
static inline void pc_bswap_restimg(void* p) {
    uint8_t* d = (uint8_t*)p;
    /* 0x02: u16 width */
    *(uint16_t*)(d+0x02) = pc_bswap16(*(uint16_t*)(d+0x02));
    /* 0x04: u16 height */
    *(uint16_t*)(d+0x04) = pc_bswap16(*(uint16_t*)(d+0x04));
    /* 0x0A: u16 numColors */
    *(uint16_t*)(d+0x0A) = pc_bswap16(*(uint16_t*)(d+0x0A));
    /* 0x0C: u32 paletteOffset */
    *(uint32_t*)(d+0x0C) = pc_bswap32(*(uint32_t*)(d+0x0C));
    /* 0x1A: s16 LODBias */
    *(uint16_t*)(d+0x1A) = pc_bswap16(*(uint16_t*)(d+0x1A));
    /* 0x1C: u32 imageOffset */
    *(uint32_t*)(d+0x1C) = pc_bswap32(*(uint32_t*)(d+0x1C));
}

/* Byte-swap a ResNTAB name table in-place.
 * Layout: u16 entryNum, u16 pad, then entryNum * {u16 keyCode, u16 offs} */
static inline void pc_bswap_resntab(void* p) {
    uint8_t* d = (uint8_t*)p;
    uint16_t entryNum = pc_bswap16(*(uint16_t*)d);
    *(uint16_t*)(d+0) = entryNum;
    *(uint16_t*)(d+2) = pc_bswap16(*(uint16_t*)(d+2)); /* pad */
    /* Each entry: u16 keyCode + u16 offs = 4 bytes */
    pc_bswap16_array(d + 4, entryNum * 2);
}

/* Byte-swap all data within a J3D block, based on block type.
 * Call AFTER the block header (type+size) has been swapped.
 * blockData points to the block header (type at +0, size at +4). */
static inline void pc_j3d_bswap_block(uint32_t blockType, uint8_t* blockData, uint32_t blockSize) {
    /* Swap fields AFTER the 8-byte header (type+size already swapped) */
    uint8_t* d = blockData;

    switch (blockType) {
    case 0x494E4631: /* 'INF1' */ {
        /* Some archives contain INF1 header words with the first two u16
         * swapped as 0xFFFF0000 instead of 0x0000FFFF. Repair before endian
         * conversion so mFlags and the following field land correctly. */
        if (d[8] == 0xFF && d[9] == 0xFF && d[10] == 0x00 && d[11] == 0x00) {
            uint16_t hi = *(uint16_t*)(d + 0x08);
            uint16_t lo = *(uint16_t*)(d + 0x0A);
            *(uint16_t*)(d + 0x08) = lo;
            *(uint16_t*)(d + 0x0A) = hi;
        }
        /* 0x08: u16 mFlags */
        static int s_inf = 0;
        uint16_t pre = *(uint16_t*)(d+0x08);
        *(uint16_t*)(d+0x08) = pc_bswap16(pre);
        if (s_inf++ < 5) {
            fprintf(stderr, "[BSWAP] INF1 #%d: mFlags pre=0x%04x post=0x%04x raw[8..B]=%02x%02x%02x%02x\n",
                    s_inf, pre, *(uint16_t*)(d+0x08), d[8], d[9], d[10], d[11]);
            fflush(stderr);
        }
        /* 0x0C: u32 mPacketNum */
        *(uint32_t*)(d+0x0C) = pc_bswap32(*(uint32_t*)(d+0x0C));
        /* 0x10: u32 mVtxNum */
        *(uint32_t*)(d+0x10) = pc_bswap32(*(uint32_t*)(d+0x10));
        /* 0x14: u32 mpHierarchy (offset) */
        *(uint32_t*)(d+0x14) = pc_bswap32(*(uint32_t*)(d+0x14));
        /* Hierarchy data: J3DModelHierarchy = {u16 mValue, u16 mType}.
         * End marker = {0, 0}. Swap all entries, then normalize layout order.
         * Bug fix: previous code checked only the first u16 for termination,
         * causing premature exit when mValue==0 (e.g., joint/shape index 0). */
        if (*(uint32_t*)(d+0x14) != 0) {
            uint32_t hierOff = *(uint32_t*)(d+0x14);
            uint8_t* hier = d + hierOff;
            while (hier < d + blockSize - 4) {
                uint16_t first = pc_bswap16(*(uint16_t*)hier);
                uint16_t second = pc_bswap16(*(uint16_t*)(hier+2));
                *(uint16_t*)hier = first;
                *(uint16_t*)(hier+2) = second;
                if (first == 0 && second == 0) break;
                hier += 4;
            }
            pc_j3d_normalize_inf1_hierarchy(d + hierOff, blockSize, hierOff);
        }
        } break;

    case 0x56545831: /* 'VTX1' */
        /* 0x08-0x3F: 14 offset fields (u32 each) */
        pc_bswap32_array(d+0x08, 14);
        /* Vertex attribute format list at offset 0x08 */
        if (*(uint32_t*)(d+0x08) != 0) {
            uint8_t* fmtList = d + *(uint32_t*)(d+0x08);
            /* Array of {u32 attr, u32 cnt, u32 type, u8 frac, pad[3]} = 16 bytes each */
            /* Terminated by attr == 0xFF (GX_VA_NULL) */
            while (fmtList < d + blockSize - 16) {
                uint32_t attr = pc_bswap32(*(uint32_t*)fmtList);
                *(uint32_t*)(fmtList+0) = attr;
                *(uint32_t*)(fmtList+4) = pc_bswap32(*(uint32_t*)(fmtList+4));
                *(uint32_t*)(fmtList+8) = pc_bswap32(*(uint32_t*)(fmtList+8));
                /* byte 12 is frac (u8), no swap needed */
                if (attr == 0xFF) break;
                fmtList += 16;
            }
        }
        /* Vertex data arrays (positions, normals, colors, texcoords) are accessed
         * as raw byte arrays by the GX system — their endianness is handled by
         * the vertex submission functions. We swap individual vertex elements
         * based on their format when submitting to GX. For now, leave raw. */
        break;

    case 0x45565031: /* 'EVP1' */
        /* 0x08: u16 mWEvlpMtxNum */
        *(uint16_t*)(d+0x08) = pc_bswap16(*(uint16_t*)(d+0x08));
        /* 0x0C-0x18: 4 offset fields (u32) */
        pc_bswap32_array(d+0x0C, 4);
        {
            uint16_t mtxNum = *(uint16_t*)(d+0x08);
            uint32_t mixNumOff = *(uint32_t*)(d+0x0C);
            uint32_t mixIdxOff = *(uint32_t*)(d+0x10);
            uint32_t mixWgtOff = *(uint32_t*)(d+0x14);
            uint32_t invMtxOff = *(uint32_t*)(d+0x18);

            auto next_offset = [&](uint32_t off) -> uint32_t {
                uint32_t next = blockSize;
                const uint32_t offsets[] = {mixNumOff, mixIdxOff, mixWgtOff, invMtxOff};
                for (uint32_t candidate : offsets) {
                    if (candidate > off && candidate < next) {
                        next = candidate;
                    }
                }
                return next;
            };

            if (mtxNum == 0xFFFF) {
                uint32_t derivedMtxNum = 0;
                if (mixNumOff != 0 && mixNumOff < blockSize) {
                    derivedMtxNum = next_offset(mixNumOff) - mixNumOff;
                } else if (invMtxOff != 0 && invMtxOff < blockSize) {
                    derivedMtxNum = (blockSize - invMtxOff) / (12 * sizeof(uint32_t));
                }
                if (derivedMtxNum > 0 && derivedMtxNum < 0x400) {
                    mtxNum = (uint16_t)derivedMtxNum;
                    *(uint16_t*)(d+0x08) = mtxNum;
                }
            }

            size_t totalMix = 0;
            if (mixNumOff != 0 && mixNumOff < blockSize) {
                size_t mixNumCount = next_offset(mixNumOff) - mixNumOff;
                if (mixNumCount > mtxNum) {
                    mixNumCount = mtxNum;
                }

                uint8_t* mixNums = d + mixNumOff;
                for (size_t i = 0; i < mixNumCount; i++) {
                    totalMix += mixNums[i];
                }
            }

            if (mixIdxOff != 0 && mixIdxOff < blockSize) {
                size_t maxMixIdx = (next_offset(mixIdxOff) - mixIdxOff) / sizeof(uint16_t);
                if (totalMix > maxMixIdx) {
                    totalMix = maxMixIdx;
                }
                if (totalMix > 0) {
                    pc_bswap16_array(d + mixIdxOff, totalMix);
                }
            }

            if (mixWgtOff != 0 && mixWgtOff < blockSize) {
                size_t maxMixWgt = (next_offset(mixWgtOff) - mixWgtOff) / sizeof(uint32_t);
                size_t mixWgtCount = totalMix < maxMixWgt ? totalMix : maxMixWgt;
                if (mixWgtCount > 0) {
                    pc_bswap32_array(d + mixWgtOff, mixWgtCount);
                }
            }

            if (invMtxOff != 0 && invMtxOff < blockSize) {
                size_t maxInvMtx = (blockSize - invMtxOff) / (12 * sizeof(uint32_t));
                size_t invMtxCount = mtxNum < maxInvMtx ? mtxNum : maxInvMtx;
                if (invMtxCount > 0) {
                    pc_bswap32_array(d + invMtxOff, invMtxCount * 12);
                }
            }
        }
        break;

    case 0x44525731: /* 'DRW1' */
        /* 0x08: u16 mMtxNum */
        *(uint16_t*)(d+0x08) = pc_bswap16(*(uint16_t*)(d+0x08));
        /* 0x0C: u32 mpDrawMtxFlag (offset) */
        *(uint32_t*)(d+0x0C) = pc_bswap32(*(uint32_t*)(d+0x0C));
        /* 0x10: u32 mpDrawMtxIndex (offset) */
        *(uint32_t*)(d+0x10) = pc_bswap32(*(uint32_t*)(d+0x10));
        /* DrawMtxFlag: array of u8 — no swap */
        /* DrawMtxIndex: array of u16 */
        {
            uint16_t mtxNum = *(uint16_t*)(d+0x08);
            uint32_t flagOff = *(uint32_t*)(d+0x0C);
            uint32_t idxOff = *(uint32_t*)(d+0x10);
            if (mtxNum == 0xFFFF && flagOff != 0 && idxOff > flagOff && idxOff < blockSize) {
                uint32_t flagCount = idxOff - flagOff;
                uint32_t idxCount = (blockSize - idxOff) / sizeof(uint16_t);
                uint32_t derivedMtxNum = flagCount < idxCount ? flagCount : idxCount;
                if (derivedMtxNum > 0 && derivedMtxNum < 0x4000) {
                    mtxNum = (uint16_t)derivedMtxNum;
                    *(uint16_t*)(d+0x08) = mtxNum;
                }
            }
            if (idxOff != 0 && idxOff < blockSize && mtxNum > 0) {
                pc_bswap16_array(d + idxOff, mtxNum);
            }
        }
        break;

    case 0x4A4E5431: /* 'JNT1' */
        /* 0x08: u16 mJointNum */
        *(uint16_t*)(d+0x08) = pc_bswap16(*(uint16_t*)(d+0x08));
        /* 0x0C-0x14: 3 offset fields */
        pc_bswap32_array(d+0x0C, 3);
        {
            uint16_t jntNum = *(uint16_t*)(d+0x08);
            /* Joint init data at offset 0x0C: array of J3DJointInitData (0x40 bytes each)
             * Fields: u16 kind, u8, u8, then J3DTransformInfo + f32 + Vec + Vec */
            uint32_t jntOff = *(uint32_t*)(d+0x0C);
            uint32_t idxOff = *(uint32_t*)(d+0x10);
            uint32_t nameOff = *(uint32_t*)(d+0x14);
            if (jntNum == 0xFFFF && jntOff != 0 && idxOff > jntOff) {
                uint32_t derivedJntNum = (idxOff - jntOff) / 0x40;
                if (nameOff != 0 && nameOff > idxOff) {
                    uint32_t idxCount = (nameOff - idxOff) / sizeof(uint16_t);
                    if (idxCount != 0 && idxCount < derivedJntNum) {
                        derivedJntNum = idxCount;
                    }
                }
                if (derivedJntNum > 0 && derivedJntNum < 0x400) {
                    jntNum = (uint16_t)derivedJntNum;
                    *(uint16_t*)(d+0x08) = jntNum;
                }
            }
            if (jntOff != 0) {
                for (int i = 0; i < jntNum; i++) {
                    uint8_t* j = d + jntOff + i * 0x40;
                    /* 0x00: u16 mKind */
                    *(uint16_t*)(j+0x00) = pc_bswap16(*(uint16_t*)(j+0x00));
                    /* 0x04-0x0C: Vec mScale (3x f32) */
                    pc_bswap32_array(j+0x04, 3);
                    /* 0x10-0x14: S16Vec mRotation (3x s16) */
                    pc_bswap16_array(j+0x10, 3);
                    /* 0x18-0x24: Vec mTranslate (3x f32) — starts at 0x18 due to padding */
                    pc_bswap32_array(j+0x18, 3);
                    /* 0x24: f32 mRadius */
                    pc_bswap_f32(j+0x24);
                    /* 0x28-0x30: Vec mMin (3x f32) */
                    pc_bswap32_array(j+0x28, 3);
                    /* 0x34-0x3C: Vec mMax (3x f32) */
                    pc_bswap32_array(j+0x34, 3);
                }
            }
            /* Index table at offset 0x10: array of u16 */
            if (idxOff != 0 && idxOff < blockSize && jntNum > 0) {
                pc_bswap16_array(d + idxOff, jntNum);
            }
            /* Name table at offset 0x14 */
            if (nameOff != 0 && nameOff < blockSize) {
                pc_bswap_resntab(d + nameOff);
            }
        }
        break;

    case 0x4D415433: /* 'MAT3' */
    case 0x4D415432: /* 'MAT2' */
        /* 0x08: u16 mMaterialNum */
        *(uint16_t*)(d+0x08) = pc_bswap16(*(uint16_t*)(d+0x08));
        /* 0x0C-0x80: ~30 offset fields (u32 each) */
        pc_bswap32_array(d+0x0C, (0x84 - 0x0C) / 4);
        {
            uint16_t matNum = *(uint16_t*)(d+0x08);
            /* Swap MAT3 data tables using offset gaps to determine sizes.
             * Offsets are at 0x0C..0x80 (30 u32 entries, already swapped).
             * Each table's byte size = next_nonzero_offset - this_offset. */
            {
                /* Collect all offsets to compute table sizes */
                uint32_t offsets[30];
                for (int k = 0; k < 30; k++)
                    offsets[k] = *(uint32_t*)(d + 0x0C + k * 4);

                /* Helper: find the size of table at offsets[idx] */
                auto tableBytes = [&](int idx) -> uint32_t {
                    uint32_t off = offsets[idx];
                    if (off == 0 || off >= blockSize) return 0;
                    /* Find next non-zero offset after this one */
                    uint32_t nextOff = blockSize;
                    for (int k = 0; k < 30; k++) {
                        if (offsets[k] > off && offsets[k] < nextOff)
                            nextOff = offsets[k];
                    }
                    return nextOff - off;
                    };

                /* Some MAT3 payloads arrive pre-swapped at header/offset level but
                 * table payload is still word-swapped. Normalize tables first, then
                 * run regular field-level swapping below. */
                {
                    bool needWordUnswap = false;
                    uint32_t initOff = offsets[0];
                    uint32_t initBytes = tableBytes(0);
                    if (initOff != 0 && initOff + 4 <= blockSize && initBytes >= 4) {
                        uint8_t b0 = d[initOff + 0];
                        uint8_t b1 = d[initOff + 1];
                        uint8_t b2 = d[initOff + 2];
                        uint8_t b3 = d[initOff + 3];
                        if (b0 == 0 && b1 == 0 && b2 == 0 && b3 > 0 && b3 <= 7) {
                            needWordUnswap = true;
                        }
                    }

                    if (!needWordUnswap) {
                        uint32_t ccOff = offsets[6];
                        uint32_t ccBytes = tableBytes(6);
                        if (ccOff != 0 && ccOff + 4 <= blockSize && ccBytes >= 4) {
                            uint8_t b0 = d[ccOff + 0];
                            uint8_t b3 = d[ccOff + 3];
                            if (b0 > 0x20 && (b3 <= 0x10 || b3 == 0xFF)) {
                                needWordUnswap = true;
                            }
                        }
                    }

                    if (needWordUnswap) {
                        for (int k = 0; k < 30; k++) {
                            uint32_t off = offsets[k];
                            uint32_t bytes = tableBytes(k);
                            if (off == 0 || off >= blockSize || bytes < 4) {
                                continue;
                            }
                            pc_j3d_unswap_words(d + off, bytes);
                        }
                    }
                }

                /* 0: Material init data */
                if (offsets[0] != 0 && offsets[0] < blockSize) {
                    size_t initCount = tableBytes(0) / 0x14C;
                    if (initCount > matNum) {
                        initCount = matNum;
                    }

                    for (size_t i = 0; i < initCount; i++) {
                        uint8_t* m = d + offsets[0] + i * 0x14C;
                        pc_bswap16_array(m+0x08, 2);
                        pc_bswap16_array(m+0x0C, 4);
                        pc_bswap16_array(m+0x14, 2);
                        pc_bswap16_array(m+0x28, 8);
                        pc_bswap16_array(m+0x48, 8);
                        pc_bswap16_array(m+0x84, 8);
                        pc_bswap16_array(m+0x94, 4);
                        pc_bswap16_array(m+0xBC, 16);
                        pc_bswap16_array(m+0xDC, 4);
                        pc_bswap16_array(m+0xE4, 16);
                        pc_bswap16_array(m+0x104, 16);
                        pc_bswap16_array(m+0x124, 4);
                        *(uint16_t*)(m+0x144) = pc_bswap16(*(uint16_t*)(m+0x144));
                        *(uint16_t*)(m+0x146) = pc_bswap16(*(uint16_t*)(m+0x146));
                        *(uint16_t*)(m+0x148) = pc_bswap16(*(uint16_t*)(m+0x148));
                        *(uint16_t*)(m+0x14A) = pc_bswap16(*(uint16_t*)(m+0x14A));
                    }
                }

                /* 1: Material ID array */
                if (offsets[1] != 0 && offsets[1] < blockSize) {
                    size_t matIdCount = tableBytes(1) / sizeof(uint16_t);
                    if (matIdCount > matNum) {
                        matIdCount = matNum;
                    }
                    if (matIdCount > 0) {
                        pc_bswap16_array(d + offsets[1], matIdCount);
                    }
                }

                /* Table index mapping (offset position - 0x0C) / 4:
                 *  0: mpMaterialInitData  — swapped above
                 *  1: mpMaterialID        — swapped above
                 *  2: mpNameTable         — swap as ResNTAB
                 *  3: mpIndInitData       — complex struct, skip for now
                 *  4: mpCullMode          — u32 array
                 *  5: mpMatColor          — GXColor (4 bytes), no swap
                 *  6: mpColorChanNum      — u8 array, no swap
                 *  7: mpColorChanInfo     — J3DColorChanInfo has u16 fields
                 *  8: mpAmbColor          — GXColor (4 bytes), no swap
                 *  9: mpLightInfo         — complex, skip
                 * 10: mpTexGenNum         — u8 array, no swap
                 * 11: mpTexCoordInfo      — J3DTexCoordInfo (4 bytes each)
                 * 12: mpTexCoord2Info     — same
                 * 13: mpTexMtxInfo        — complex (100 bytes each), has f32s
                 * 14: field_0x44          — unknown
                 * 15: mpTexNo             — u16 array
                 * 16: mpTevOrderInfo      — J3DTevOrderInfo (4 bytes, has u8s only? no swap)
                 * 17: mpTevColor          — GXColorS10 (4x s16 = 8 bytes each)
                 * 18: mpTevKColor         — GXColor (4 bytes), no swap
                 * 19: mpTevStageNum       — u8 array, no swap
                 * 20: mpTevStageInfo      — has u8 fields only, no swap
                 * 21: mpTevSwapModeInfo   — u8 fields, no swap
                 * 22: mpTevSwapModeTableInfo — u8 fields, no swap
                 * 23: mpFogInfo           — has f32, u32, u16 fields
                 * 24: mpAlphaCompInfo     — u8 fields + u32, needs swap
                 * 25: mpBlendInfo         — u8 fields only, no swap
                 * 26: mpZModeInfo         — u8 fields only, no swap
                 * 27: mpZCompLoc          — u8, no swap
                 * 28: mpDither            — u8, no swap
                 * 29: mpNBTScaleInfo      — has f32 fields
                 */

                /* 2: Name table */
                if (offsets[2] != 0 && offsets[2] < blockSize) {
                    pc_bswap_resntab(d + offsets[2]);
                }

                /* 4: CullMode — u32 array */
                {
                    uint32_t bytes = tableBytes(4);
                    if (bytes > 0) pc_bswap32_array(d + offsets[4], bytes / 4);
                }

                /* 15: TexNo — u16 array */
                {
                    uint32_t bytes = tableBytes(15);
                    if (bytes > 0) pc_bswap16_array(d + offsets[15], bytes / 2);
                }

                /* 17: TevColor — GXColorS10 = 4x s16 = 8 bytes each */
                {
                    uint32_t bytes = tableBytes(17);
                    if (bytes > 0) pc_bswap16_array(d + offsets[17], bytes / 2);
                }

                /* 23: FogInfo — 44 bytes each: u8 type, pad[3], f32 startZ, f32 endZ,
                 *   f32 nearZ, f32 farZ, GXColor color, u16 adjTable[10] */
                {
                    uint32_t bytes = tableBytes(23);
                    if (bytes > 0) {
                        int count = bytes / 44;
                        for (int fi = 0; fi < count; fi++) {
                            uint8_t* fog = d + offsets[23] + fi * 44;
                            pc_bswap32_array(fog + 4, 4); /* 4x f32 */
                            pc_bswap16_array(fog + 24, 10); /* u16[10] adj table */
                        }
                    }
                }

                /* 9: LightInfo — 0x34 bytes each: Vec pos(3xf32), Vec dir(3xf32), GXColor(4xu8), Vec cosAtten(3xf32), Vec distAtten(3xf32) */
                {
                    uint32_t bytes = tableBytes(9);
                    if (bytes > 0) {
                        int count = bytes / 0x34;
                        for (int li = 0; li < count; li++) {
                            uint8_t* lt = d + offsets[9] + li * 0x34;
                            pc_bswap32_array(lt + 0x00, 3); /* pos */
                            pc_bswap32_array(lt + 0x0C, 3); /* dir */
                            /* 0x18: GXColor = 4xu8, no swap */
                            pc_bswap32_array(lt + 0x1C, 3); /* cosAtten */
                            pc_bswap32_array(lt + 0x28, 3); /* distAtten */
                        }
                    }
                }

                /* 13: TexMtxInfo — 0x64 bytes each: u8[4], Vec center(3xf32+pad),
                 *   SRTInfo(f32 scaleX,scaleY, s16 rot, pad, f32 transX,transY = 0x14 bytes),
                 *   Mtx44 effectMtx (16xf32) */
                {
                    uint32_t bytes = tableBytes(13);
                    if (bytes > 0) {
                        int count = bytes / 0x64;
                        for (int ti = 0; ti < count; ti++) {
                            uint8_t* tm = d + offsets[13] + ti * 0x64;
                            /* 0x04: Vec center (3xf32) */
                            pc_bswap32_array(tm + 0x04, 3);
                            /* 0x10: SRT — f32 scaleX, f32 scaleY, s16 rotation, pad, f32 transX, f32 transY */
                            pc_bswap32_array(tm + 0x10, 2); /* scaleX, scaleY */
                            *(uint16_t*)(tm + 0x18) = pc_bswap16(*(uint16_t*)(tm + 0x18)); /* rotation */
                            pc_bswap32_array(tm + 0x1C, 2); /* transX, transY */
                            /* 0x24: Mtx44 effectMtx (16 f32) */
                            pc_bswap32_array(tm + 0x24, 16);
                        }
                    }
                }

                /* 14: field_0x44 (post-TexMtx) — same format as TexMtxInfo */
                {
                    uint32_t bytes = tableBytes(14);
                    if (bytes > 0) {
                        int count = bytes / 0x64;
                        for (int ti = 0; ti < count; ti++) {
                            uint8_t* tm = d + offsets[14] + ti * 0x64;
                            pc_bswap32_array(tm + 0x04, 3);
                            pc_bswap32_array(tm + 0x10, 2);
                            *(uint16_t*)(tm + 0x18) = pc_bswap16(*(uint16_t*)(tm + 0x18));
                            pc_bswap32_array(tm + 0x1C, 2);
                            pc_bswap32_array(tm + 0x24, 16);
                        }
                    }
                }

                /* 29: NBTScaleInfo — 16 bytes: u8 enable, pad[3], Vec scale (3x f32) */
                {
                    uint32_t bytes = tableBytes(29);
                    if (bytes > 0) {
                        int count = bytes / 16;
                        for (int ni = 0; ni < count; ni++) {
                            pc_bswap32_array(d + offsets[29] + ni * 16 + 4, 3);
                        }
                    }
                }

                /* Repair word-swapped byte-oriented MAT3 tables. This can happen
                 * when a resource was preprocessed in 32-bit chunks before proper
                 * MAT3 field swapping. */
                {
                    int suspicious = 0;
                    const int probeIdx[3] = {6, 10, 19};
                    for (int pi = 0; pi < 3; pi++) {
                        uint32_t off = offsets[probeIdx[pi]];
                        uint32_t bytes = tableBytes(probeIdx[pi]);
                        if (off == 0 || off + 4 > blockSize || bytes < 4) {
                            continue;
                        }
                        uint8_t b0 = d[off + 0];
                        uint8_t b3 = d[off + 3];
                        if (b0 > 0x20 && (b3 <= 0x10 || b3 == 0xFF)) {
                            suspicious++;
                        }
                    }

                    if (suspicious >= 2) {
                        const int byteTables[] = {
                            5, 6, 8, 10, 11, 12, 16, 18, 19, 20, 21, 22, 25, 26, 27, 28
                        };
                        for (size_t bi = 0; bi < sizeof(byteTables) / sizeof(byteTables[0]); bi++) {
                            int idx = byteTables[bi];
                            uint32_t off = offsets[idx];
                            uint32_t bytes = tableBytes(idx);
                            if (off == 0 || off >= blockSize || bytes < 4) {
                                continue;
                            }
                            pc_j3d_unswap_words(d + off, bytes);
                        }
                    }
                }
            }
        }
        break;

    case 0x53485031: /* 'SHP1' */
        /* 0x08: u16 mShapeNum */
        *(uint16_t*)(d+0x08) = pc_bswap16(*(uint16_t*)(d+0x08));
        /* 0x0C-0x28: 8 offset fields */
        pc_bswap32_array(d+0x0C, 8);
        {
            uint16_t shpNum = *(uint16_t*)(d+0x08);
            /* Shape init data at offset 0x0C: shpNum * 0x28 bytes */
            uint32_t shpOff = *(uint32_t*)(d+0x0C);
            uint32_t idxOff = *(uint32_t*)(d+0x10);
            uint32_t nameOff = *(uint32_t*)(d+0x14);
            if (shpNum == 0xFFFF && shpOff != 0 && idxOff > shpOff) {
                uint32_t derivedShpNum = (idxOff - shpOff) / 0x28;
                if (nameOff != 0 && nameOff > idxOff) {
                    uint32_t idxCount = (nameOff - idxOff) / sizeof(uint16_t);
                    if (idxCount != 0 && idxCount < derivedShpNum) {
                        derivedShpNum = idxCount;
                    }
                }
                if (derivedShpNum > 0 && derivedShpNum < 0x400) {
                    shpNum = (uint16_t)derivedShpNum;
                    *(uint16_t*)(d+0x08) = shpNum;
                }
            }
            if (shpOff != 0) {
                for (int i = 0; i < shpNum; i++) {
                    uint8_t* s = d + shpOff + i * 0x28;
                    /* 0x02: u16 mMtxGroupNum */
                    *(uint16_t*)(s+0x02) = pc_bswap16(*(uint16_t*)(s+0x02));
                    /* 0x04: u16 mVtxDescListIndex */
                    *(uint16_t*)(s+0x04) = pc_bswap16(*(uint16_t*)(s+0x04));
                    /* 0x06: u16 mMtxInitDataIndex */
                    *(uint16_t*)(s+0x06) = pc_bswap16(*(uint16_t*)(s+0x06));
                    /* 0x08: u16 mDrawInitDataIndex */
                    *(uint16_t*)(s+0x08) = pc_bswap16(*(uint16_t*)(s+0x08));
                    /* 0x0C: f32 mRadius */
                    pc_bswap_f32(s+0x0C);
                    /* 0x10-0x18: Vec mMin */
                    pc_bswap32_array(s+0x10, 3);
                    /* 0x1C-0x24: Vec mMax */
                    pc_bswap32_array(s+0x1C, 3);
                }
            }
            /* Index table at offset 0x10: u16 array */
            if (idxOff != 0 && idxOff < blockSize && shpNum > 0) {
                pc_bswap16_array(d + idxOff, shpNum);
            }
            /* MtxInitData at offset 0x24: array of {u16, u16, u32} = 8 bytes */
            uint32_t mtxInitOff = *(uint32_t*)(d+0x24);
            if (mtxInitOff != 0) {
                /* Count from shape data */
                int totalMtxGrp2 = 0;
                if (shpOff != 0) {
                    for (int i = 0; i < shpNum; i++) {
                        uint8_t* s = d + shpOff + i * 0x28;
                        totalMtxGrp2 += *(uint16_t*)(s+0x02); /* already swapped */
                    }
                }
                for (int i = 0; i < totalMtxGrp2; i++) {
                    uint8_t* mi = d + mtxInitOff + i * 8;
                    *(uint16_t*)(mi+0) = pc_bswap16(*(uint16_t*)(mi+0));
                    *(uint16_t*)(mi+2) = pc_bswap16(*(uint16_t*)(mi+2));
                    *(uint32_t*)(mi+4) = pc_bswap32(*(uint32_t*)(mi+4));
                }
            }
            /* DrawInitData at offset 0x28: array of {u32 size, u32 offset} = 8 bytes */
            uint32_t drawInitOff = *(uint32_t*)(d+0x28);
            if (drawInitOff != 0) {
                int totalMtxGrp = 0;
                if (shpOff != 0) {
                    for (int i = 0; i < shpNum; i++) {
                        uint8_t* s = d + shpOff + i * 0x28;
                        totalMtxGrp += *(uint16_t*)(s+0x02);
                    }
                }
                pc_bswap32_array(d + drawInitOff, totalMtxGrp * 2);
            }
            /* MtxTable at offset 0x1C: u16 array.
             * IMPORTANT: only swap up to the next known data section to avoid
             * corrupting MtxInitData/DrawInitData with u16 swaps. */
            uint32_t mtxTblOff = *(uint32_t*)(d+0x1C);
            if (mtxTblOff != 0 && mtxTblOff < blockSize) {
                /* Find the end of the MtxTable: it ends at the start of the next section */
                uint32_t mtxTblEnd = blockSize;
                uint32_t mio = *(uint32_t*)(d+0x24); /* mtxInitData offset */
                uint32_t dio = *(uint32_t*)(d+0x28); /* drawInitData offset */
                uint32_t dlo = *(uint32_t*)(d+0x20); /* displayListData offset */
                if (mio > mtxTblOff && mio < mtxTblEnd) mtxTblEnd = mio;
                if (dio > mtxTblOff && dio < mtxTblEnd) mtxTblEnd = dio;
                if (dlo > mtxTblOff && dlo < mtxTblEnd) mtxTblEnd = dlo;
                int maxEntries = (mtxTblEnd - mtxTblOff) / 2;
                if (maxEntries > 4096) maxEntries = 4096;
                if (maxEntries > 0)
                    pc_bswap16_array(d + mtxTblOff, maxEntries);
            }
            /* VtxDescList at offset 0x18: array of {u32 attr, u32 type} pairs */
            uint32_t vcdOff = *(uint32_t*)(d+0x18);
            if (vcdOff != 0 && vcdOff < blockSize) {
                /* Swap only VtxDesc lists referenced by shape init entries.
                 * A broad region swap can corrupt DisplayListData in SHP1. */
                uint32_t vcdEnd = blockSize;
                uint32_t mtxTbl = *(uint32_t*)(d+0x1C);
                uint32_t dlOff = *(uint32_t*)(d+0x20);
                uint32_t mtxInit = *(uint32_t*)(d+0x24);
                uint32_t drawInit = *(uint32_t*)(d+0x28);
                if (nameOff > vcdOff && nameOff < vcdEnd) vcdEnd = nameOff;
                if (mtxTbl > vcdOff && mtxTbl < vcdEnd) vcdEnd = mtxTbl;
                if (dlOff > vcdOff && dlOff < vcdEnd) vcdEnd = dlOff;
                if (mtxInit > vcdOff && mtxInit < vcdEnd) vcdEnd = mtxInit;
                if (drawInit > vcdOff && drawInit < vcdEnd) vcdEnd = drawInit;

                if (shpOff != 0 && vcdEnd > vcdOff) {
                    uint16_t seenIdx[512];
                    uint32_t seenCount = 0;
                    for (uint32_t si = 0; si < shpNum && si < 512; si++) {
                        const uint8_t* s = d + shpOff + si * 0x28;
                        uint16_t vtxDescIdx = *(uint16_t*)(s + 0x04); /* already swapped */

                        bool alreadySeen = false;
                        for (uint32_t k = 0; k < seenCount; k++) {
                            if (seenIdx[k] == vtxDescIdx) {
                                alreadySeen = true;
                                break;
                            }
                        }
                        if (alreadySeen) {
                            continue;
                        }
                        seenIdx[seenCount++] = vtxDescIdx;

                        uint32_t listOff = vcdOff + (uint32_t)vtxDescIdx;
                        if (listOff < vcdOff || listOff + 8 > vcdEnd) {
                            continue;
                        }

                        uint8_t* vcd = d + listOff;
                        uint8_t* vcdLimit = d + vcdEnd;
                        uint32_t rawAttr0 = *(uint32_t*)(vcd + 0);
                        uint32_t rawType0 = *(uint32_t*)(vcd + 4);
                        uint32_t swpAttr0 = pc_bswap32(rawAttr0);
                        uint32_t swpType0 = pc_bswap32(rawType0);

                        int rawLooksNative =
                            pc_j3d_vcd_attr_plausible(rawAttr0) && pc_j3d_vcd_type_plausible(rawType0);
                        int swappedLooksNative =
                            pc_j3d_vcd_attr_plausible(swpAttr0) && pc_j3d_vcd_type_plausible(swpType0);
                        int needsSwap = swappedLooksNative && !rawLooksNative;

                        if (needsSwap) {
                            uint32_t guard = 0;
                            while (vcd + 8 <= vcdLimit && guard++ < 64) {
                                uint32_t attr = pc_bswap32(*(uint32_t*)(vcd + 0));
                                uint32_t type = pc_bswap32(*(uint32_t*)(vcd + 4));
                                *(uint32_t*)(vcd + 0) = attr;
                                *(uint32_t*)(vcd + 4) = type;
                                if (attr == 0xFF) {
                                    break;
                                }
                                vcd += 8;
                            }
                        }
                    }
                }
            }
            /* Name table at offset 0x14 */
            if (nameOff != 0 && nameOff < blockSize) {
                pc_bswap_resntab(d + nameOff);
            }
        }
        break;

    case 0x54455831: /* 'TEX1' */
        /* 0x08: u16 mTextureNum */
        *(uint16_t*)(d+0x08) = pc_bswap16(*(uint16_t*)(d+0x08));
        /* 0x0C-0x10: 2 offset fields */
        pc_bswap32_array(d+0x0C, 2);
        {
            uint16_t texNum = *(uint16_t*)(d+0x08);
            /* Texture data at offset 0x0C: texNum * ResTIMG (0x20 bytes each) */
            uint32_t texOff = *(uint32_t*)(d+0x0C);
            if (texOff != 0) {
                for (int i = 0; i < texNum; i++) {
                    pc_bswap_restimg(d + texOff + i * 0x20);
                }
            }
            /* Name table at offset 0x10 */
            uint32_t nameOff = *(uint32_t*)(d+0x10);
            if (nameOff != 0 && nameOff < blockSize) {
                pc_bswap_resntab(d + nameOff);
            }
        }
        break;

    /* Animation blocks — swap header fields and data arrays */
    case 0x414E4B31: /* 'ANK1' - Transform key animation (BCK) */
        /* 0x0A: s16 duration, 0x0C: s16 keyframe_num, 0x0E-0x12: s16 entries */
        pc_bswap16_array(d+0x0A, 4);
        /* 0x14-0x20: 4x u32 offsets (anm_data, scale, rotation, translation) */
        pc_bswap32_array(d+0x14, 4);
        {
            /* Scale data: f32 array */
            uint32_t scaleOff = *(uint32_t*)(d+0x18);
            int16_t scaleN = *(int16_t*)(d+0x0E);
            if (scaleOff != 0 && scaleOff < blockSize && scaleN > 0)
                pc_bswap32_array(d + scaleOff, scaleN);
            /* Rotation data: s16 array */
            uint32_t rotOff = *(uint32_t*)(d+0x1C);
            int16_t rotN = *(int16_t*)(d+0x10);
            if (rotOff != 0 && rotOff < blockSize && rotN > 0)
                pc_bswap16_array(d + rotOff, rotN);
            /* Translation data: f32 array */
            uint32_t transOff = *(uint32_t*)(d+0x20);
            int16_t transN = *(int16_t*)(d+0x12);
            if (transOff != 0 && transOff < blockSize && transN > 0)
                pc_bswap32_array(d + transOff, transN);
            /* Anm table: 3 entries per joint × 3 components (s,r,t) × 4 u16 each = 36 bytes/joint */
            uint32_t anmOff = *(uint32_t*)(d+0x14);
            int16_t jntNum = *(int16_t*)(d+0x0C);
            if (anmOff != 0 && anmOff < blockSize && jntNum > 0)
                pc_bswap16_array(d + anmOff, jntNum * 3 * 4 * 3);
        }
        break;

    case 0x54524B31: /* 'TRK1' - TEV register key (BRK) */
        /* J3DAnmTevRegKeyData: u8[2] at 0x08-0x09, then s16/u16 at 0x0A-0x1E, u32 offsets at 0x20-0x54 */
        pc_bswap16_array(d+0x0A, 11); /* 11 u16 fields from 0x0A to 0x1E */
        pc_bswap32_array(d+0x20, 14); /* 14 u32 offset fields from 0x20 to 0x54 */
        break;

    case 0x50414B31: /* 'PAK1' - Color key (BPK) */
        /* J3DAnmColorKeyData: similar layout */
        pc_bswap16_array(d+0x0A, 8); /* u16 fields */
        pc_bswap32_array(d+0x18, 6); /* u32 offsets */
        break;

    case 0x54544B31: /* 'TTK1' - Texture SRT key (BTK) */
        /* J3DAnmTextureSRTKeyData: u8[2] at 0x08-0x09, s16/u16 at 0x0A-0x12, u32 offsets at 0x14-0x3C */
        pc_bswap16_array(d+0x0A, 5);
        pc_bswap32_array(d+0x14, 11);
        break;

    case 0x54505431: /* 'TPT1' - Texture pattern (BTP) */
        pc_bswap16_array(d+0x08, 4);
        pc_bswap32_array(d+0x10, 4);
        break;

    case 0x56414631: /* 'VAF1' - Visibility (BVA) */
        pc_bswap16_array(d+0x08, 4);
        pc_bswap32_array(d+0x10, 2);
        break;

    case 0x434C4B31: /* 'CLK1' - Cluster key (BLK) */
    case 0x434C4631: /* 'CLF1' - Cluster full */
    case 0x56434B31: /* 'VCK1' - Vertex color key (BXA) */
        /* Generic: swap header s16 fields and u32 offsets */
        pc_bswap16_array(d+0x08, 4);
        pc_bswap32_array(d+0x10, 4);
        break;

    default:
        /* Unknown block type — skip */
        break;
    }
}

/* Byte-swap an entire J3D model file (BMD/BDL) in-place.
 * This swaps the file header, all block headers, and all block data. */
static inline void pc_j3d_bswap_file(void* data, size_t data_size) {
    if (!data || data_size < 0x20) return;
    if (data_size == 0x1e72c0 || data_size == 0x1560 || data_size == 0x72c0 || data_size == 0x3c20 ||
        data_size == 0x3a7e0 || data_size == 0x200c0) {
        /* Known problematic assets that are already loader-usable on PC.
         * Swapping these payloads triggers loader crashes. */
        static int s_title_skip = 0;
        if (s_title_skip++ < 8) {
            fprintf(stderr, "[J3D] bswap_file: skip known asset size=0x%zx\n", data_size);
        }
        return;
    }
    if (!pc_j3d_needs_bswap(data)) {
        /* File header already native. Check if block content still needs swap
         * by testing if the first block's content fields look big-endian.
         * This handles the case where headers were pre-swapped but content wasn't. */
        uint8_t* p = (uint8_t*)data;
        uint32_t blockNum = ((uint32_t*)data)[3];
        if (blockNum > 0 && blockNum <= 20 && data_size > 0x28) {
            /* Block headers are already native — check if content is also native.
             * Test the FIRST block (typically INF1). If it looks native, ALL blocks
             * are native (the file was fully pre-swapped) and we skip all swapping.
             * This prevents double-swapping that corrupts material/texture data. */
            uint8_t* blockPtr = p + 0x20;
            uint32_t firstSize = *(uint32_t*)(blockPtr + 4);
            uint32_t firstType = *(uint32_t*)blockPtr;  /* header already native LE */
            int allNative = pc_j3d_block_looks_native(firstType, blockPtr, firstSize);

            /* If first block test is inconclusive, also check a block with
             * more distinctive native-vs-BE signatures */
            if (!allNative) {
                uint8_t* scan = blockPtr;
                for (uint32_t i = 0; i < blockNum && scan + 8 <= p + data_size; i++) {
                    uint32_t sz = *(uint32_t*)(scan + 4);
                    if (sz < 8 || scan + sz > p + data_size) break;
                    uint32_t tLE = *(uint32_t*)scan;  /* already native LE */
                    /* JNT1 and SHP1 have very distinctive native signatures */
                    if (tLE == 0x4A4E5431 || tLE == 0x53485031 || tLE == 0x54455831) {
                        if (pc_j3d_block_looks_native(tLE, scan, sz)) {
                            allNative = 1;
                            break;
                        }
                    }
                    scan += sz;
                }
            }

            if (!allNative) {
                /* Content is still big-endian, swap each block.
                 * Block headers are already in native LE — pass the native type
                 * to bswap_block (its case labels use native LE multi-char constants). */
                uint8_t* bp = blockPtr;
                for (uint32_t i = 0; i < blockNum && bp + 8 <= p + data_size; i++) {
                    uint32_t bSize = *(uint32_t*)(bp + 4);
                    if (bSize < 8 || bp + bSize > p + data_size) break;
                    uint32_t bTypeLE = *(uint32_t*)bp;  /* already native LE */
                    if (bTypeLE == 0x4D415433 || bTypeLE == 0x4D415432) {
                        pc_j3d_fix_mat3_wordswapped_tables(bp, bSize);
                    }
                    pc_j3d_bswap_block(bTypeLE, bp, bSize);
                    bp += bSize;
                }
            } else {
                static int s_skip_log = 0;
                if (s_skip_log++ < 10) {
                    fprintf(stderr, "[J3D] bswap_file: all blocks native (pre-swapped), skipping content swap (size=%zu)\n", data_size);
                    fflush(stderr);
                }
            }
        }
        return;
    }
    /* Dump first block's raw bytes before swap */
    {
        static int s_dump = 0;
        const uint8_t* p = (const uint8_t*)data;
        if (s_dump++ < 15) {
            fprintf(stderr, "[J3D] bswap_file #%d: PRE-SWAP bytes[0x20..0x2F]=", s_dump);
            for (int i = 0x20; i < 0x30 && i < (int)data_size; i++)
                fprintf(stderr, "%02x", p[i]);
            /* Also show INF1 mFlags position after header swap */
            fprintf(stderr, " magic2=0x%02x%02x%02x%02x", p[4], p[5], p[6], p[7]);
            fprintf(stderr, "\n");
            fflush(stderr);
        }
    }

    uint8_t* p = (uint8_t*)data;
    uint32_t* u32p = (uint32_t*)data;

    /* File header */
    u32p[0] = pc_bswap32(u32p[0]); /* mMagic1 */
    u32p[1] = pc_bswap32(u32p[1]); /* mMagic2 */
    u32p[2] = pc_bswap32(u32p[2]); /* file size */
    u32p[3] = pc_bswap32(u32p[3]); /* mBlockNum */
    u32p[7] = pc_bswap32(u32p[7]); /* field_0x1c */

    uint32_t blockNum = u32p[3];
    uint8_t* blockPtr = p + 0x20;

    {
        static int s_bswap_iter_log = 0;
        bool logIter = (s_bswap_iter_log < 3);
        if (logIter) s_bswap_iter_log++;

        for (uint32_t i = 0; i < blockNum && blockPtr < p + data_size; i++) {
            uint32_t* bh = (uint32_t*)blockPtr;
            uint32_t rawType = bh[0];
            uint32_t rawSize = bh[1];
            bh[0] = pc_bswap32(bh[0]); /* block type */
            bh[1] = pc_bswap32(bh[1]); /* block size */

            uint32_t blockType = bh[0];
            uint32_t blockSize = bh[1];

            if (logIter) {
                fprintf(stderr, "[BSWAP] block %u: rawType=0x%08x rawSize=0x%08x -> type=0x%08x size=%u off=0x%lx addr=%p\n",
                        i, rawType, rawSize, blockType, blockSize,
                        (unsigned long)(blockPtr - p), (void*)blockPtr);
                /* Verify the write persisted */
                uint32_t verify = *(uint32_t*)blockPtr;
                if (verify != blockType) {
                    fprintf(stderr, "[BSWAP] WARNING: write didn't persist! wrote 0x%08x but read back 0x%08x\n",
                            blockType, verify);
                }
            }

            if (blockSize == 0 || blockSize > data_size) {
                if (logIter) {
                    fprintf(stderr, "[BSWAP] BREAK: blockSize=%u invalid (data_size=%zu)\n",
                            blockSize, data_size);
                }
                break;
            }

            /* Swap block data */
            pc_j3d_bswap_block(blockType, blockPtr, blockSize);

            blockPtr += blockSize;
        }
    }
}

#endif /* TARGET_PC */

#endif /* PC_J3D_BSWAP_H */
