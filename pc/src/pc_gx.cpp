/* pc_gx.cpp - GX graphics API implementation (OpenGL 3.3 backend)
 * Phase 3: Full rendering pipeline with state tracking, uniform uploads,
 * vertex buffering, and GL draw calls. */
#include "pc_gx_internal.h"
#include <dolphin/gx/GXEnum.h>
#include <dolphin/gd.h>
#include <setjmp.h>
#include <dolphin/vi.h>
#include <setjmp.h>

/* Crash protection from pc_main.cpp */
extern "C" void pc_crash_set_jmpbuf(jmp_buf* buf);
extern "C" uintptr_t pc_crash_get_addr(void);

PCGXState g_gx;

/* GXColor struct {u8 r,g,b,a} is passed as u32. On LE the byte layout is:
 * bit[0..7]=r, bit[8..15]=g, bit[16..23]=b, bit[24..31]=a.
 * These macros extract the correct channel on any endianness. */
#define GXCOLOR_R(c) ((c) & 0xFF)
#define GXCOLOR_G(c) (((c) >> 8) & 0xFF)
#define GXCOLOR_B(c) (((c) >> 16) & 0xFF)
#define GXCOLOR_A(c) (((c) >> 24) & 0xFF)

/* Pre-built index buffer for quad→triangle conversion */
static u16 s_quad_indices[PC_GX_MAX_QUAD_INDICES];
static u16 s_linear_indices[PC_GX_MAX_VERTS];
static GLuint s_linear_ebo = 0;
static GLuint s_boot_simple_vao = 0;
static GLuint s_boot_simple_vbo = 0;
static int s_scissor_box_off_x = 0;
static int s_scissor_box_off_y = 0;
static int s_last_loaded_tex_map = 0;
static u32 s_array_count_limit[PC_GX_MAX_ATTR];

extern "C" void pc_gx_set_array_count(u32 attr, u32 count) {
    if (attr < PC_GX_MAX_ATTR) {
        s_array_count_limit[attr] = count;
    }
}

typedef struct {
    float position[3];
    unsigned char color0[4];
    float texcoord[2];
} PCBootSimpleVertex;

static inline u32 read_be32(const u8* p);
static inline u16 read_be16(const u8* p);
static inline s16 read_be_s16(const u8* p);
static inline f32 read_be_f32(const u8* p);
static inline u32 read_le32(const u8* p);
static inline u16 read_le16(const u8* p);
static inline s16 read_le_s16(const u8* p);
static inline f32 read_le_f32(const u8* p);

static inline u32 read_be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static inline u16 read_be16(const u8* p) {
    return (u16)(((u16)p[0] << 8) | (u16)p[1]);
}

static inline s16 read_be_s16(const u8* p) {
    return (s16)read_be16(p);
}

static inline f32 read_be_f32(const u8* p) {
    union {
        u32 u;
        f32 f;
    } value;
    value.u = read_be32(p);
    return value.f;
}

static inline u32 read_le32(const u8* p) {
    return ((u32)p[3] << 24) | ((u32)p[2] << 16) | ((u32)p[1] << 8) | (u32)p[0];
}

static inline u16 read_le16(const u8* p) {
    return (u16)(((u16)p[1] << 8) | (u16)p[0]);
}

static inline s16 read_le_s16(const u8* p) {
    return (s16)read_le16(p);
}

static inline f32 read_le_f32(const u8* p) {
    union {
        u32 u;
        f32 f;
    } value;
    value.u = read_le32(p);
    return value.f;
}

static bool pc_vertex_arrays_native_le() {
    static int cached = -1;
    if (cached != -1) {
        return cached != 0;
    }
    const char* env = getenv("TP_VERTEX_ARRAY_NATIVE_LE");
    cached = (env != NULL && atoi(env) != 0) ? 1 : 0;
    return cached != 0;
}

extern "C" void GXPosition3f32(const f32 x, const f32 y, const f32 z);
extern "C" void GXNormal3f32(const f32 x, const f32 y, const f32 z);

static int pc_gx_attr_components(u32 attr, u32 cnt) {
    switch (attr) {
    case GX_VA_POS:
        return cnt == GX_POS_XY ? 2 : 3;
    case GX_VA_NRM:
        return 3;
    case GX_VA_NBT:
        return cnt == GX_NRM_NBT3 ? 9 : 3;
    case GX_VA_TEX0:
    case GX_VA_TEX1:
    case GX_VA_TEX2:
    case GX_VA_TEX3:
    case GX_VA_TEX4:
    case GX_VA_TEX5:
    case GX_VA_TEX6:
    case GX_VA_TEX7:
        return cnt == GX_TEX_S ? 1 : 2;
    default:
        return 0;
    }
}

static int pc_gx_component_size(u32 type) {
    switch (type) {
    case GX_U8:
    case GX_S8:
        return 1;
    case GX_U16:
    case GX_S16:
        return 2;
    case GX_F32:
        return 4;
    default:
        return 0;
    }
}

static int pc_gx_attr_size(u32 vtxfmt, u32 attr) {
    if (vtxfmt >= PC_GX_MAX_VTXFMT || attr >= PC_GX_MAX_ATTR) {
        return 0;
    }

    if (attr == GX_VA_PNMTXIDX || (attr >= GX_VA_TEX0MTXIDX && attr <= GX_VA_TEX7MTXIDX)) {
        return 1;
    }

    PCGXVertexFormat* fmt = &g_gx.vtx_fmt[vtxfmt];
    if (!fmt->valid[attr]) {
        return 0;
    }

    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        switch (fmt->type[attr]) {
        case GX_RGB565:
            return 2;
        case GX_RGB8:
        case GX_RGBX8:
            return 3;
        case GX_RGBA4:
            return 2;
        case GX_RGBA6:
            return 3;
        case GX_RGBA8:
            return 4;
        default:
            return 0;
        }
    }

    return pc_gx_attr_components(attr, fmt->cnt[attr]) * pc_gx_component_size(fmt->type[attr]);
}

static float pc_gx_fixed_to_float(s32 value, u32 attr, u32 type, int frac) {
    if ((attr == GX_VA_NRM || attr == GX_VA_NBT) && frac == 0) {
        if (type == GX_S8) {
            return value / 127.0f;
        }
        if (type == GX_S16) {
            return value / 32767.0f;
        }
    }

    if (frac > 0) {
        return value / (float)(1 << frac);
    }

    return (float)value;
}

static void pc_gx_apply_pos_mtx(int matrix_id, const float in[3], float out[3]) {
    int slot = matrix_id / 3;
    if (slot < 0 || slot >= 10) {
        memcpy(out, in, sizeof(float) * 3);
        return;
    }

    out[0] = g_gx.pos_mtx[slot][0][0] * in[0] + g_gx.pos_mtx[slot][0][1] * in[1] +
             g_gx.pos_mtx[slot][0][2] * in[2] + g_gx.pos_mtx[slot][0][3];
    out[1] = g_gx.pos_mtx[slot][1][0] * in[0] + g_gx.pos_mtx[slot][1][1] * in[1] +
             g_gx.pos_mtx[slot][1][2] * in[2] + g_gx.pos_mtx[slot][1][3];
    out[2] = g_gx.pos_mtx[slot][2][0] * in[0] + g_gx.pos_mtx[slot][2][1] * in[1] +
             g_gx.pos_mtx[slot][2][2] * in[2] + g_gx.pos_mtx[slot][2][3];
}

static void pc_gx_apply_nrm_mtx(int matrix_id, const float in[3], float out[3]) {
    int slot = matrix_id / 3;
    if (slot < 0 || slot >= 10) {
        memcpy(out, in, sizeof(float) * 3);
        return;
    }

    out[0] = g_gx.nrm_mtx[slot][0][0] * in[0] + g_gx.nrm_mtx[slot][0][1] * in[1] +
             g_gx.nrm_mtx[slot][0][2] * in[2];
    out[1] = g_gx.nrm_mtx[slot][1][0] * in[0] + g_gx.nrm_mtx[slot][1][1] * in[1] +
             g_gx.nrm_mtx[slot][1][2] * in[2];
    out[2] = g_gx.nrm_mtx[slot][2][0] * in[0] + g_gx.nrm_mtx[slot][2][1] * in[1] +
             g_gx.nrm_mtx[slot][2][2] * in[2];
}

static int pc_gx_decode_mtx_slot(u32 attr, u8 value) {
    if (attr == GX_VA_PNMTXIDX) {
        if (value <= GX_PNMTX9 && (value % 3) == 0) {
            return value - (value % 3);
        }
        if (value <= 9) {
            return value * 3;
        }
        return GX_PNMTX0;
    }

    if (value == GX_IDENTITY) {
        return GX_IDENTITY;
    }
    if (value >= GX_TEXMTX0 && value <= GX_TEXMTX9) {
        return value - ((value - GX_TEXMTX0) % 3);
    }
    if (value <= 9) {
        return GX_TEXMTX0 + value * 3;
    }
    return GX_IDENTITY;
}

static void pc_gx_set_active_matrix_attr(u32 attr, u8 value) {
    if (attr == GX_VA_PNMTXIDX) {
        g_gx.active_pnmtx = pc_gx_decode_mtx_slot(attr, value);
        if (g_gx.active_pnmtx < GX_PNMTX0 || g_gx.active_pnmtx > GX_PNMTX9) {
            static bool s_warned_bad_pnmtx = false;
            if (!s_warned_bad_pnmtx) {
                s_warned_bad_pnmtx = true;
                fprintf(stderr, "[GX] PNMTXIDX out of range: %u\n", value);
            }
            g_gx.active_pnmtx = GX_PNMTX0;
        }
        return;
    }

    if (attr >= GX_VA_TEX0MTXIDX && attr <= GX_VA_TEX7MTXIDX) {
        g_gx.active_texmtx[attr - GX_VA_TEX0MTXIDX] = pc_gx_decode_mtx_slot(attr, value);
    }
}

static float pc_gx_read_numeric_component(const u8* p, u32 attr, u32 type, int frac) {
    const bool useNativeLE = pc_vertex_arrays_native_le();
    if (type == GX_F32) {
        return useNativeLE ? read_le_f32(p) : read_be_f32(p);
    }
    if (type == GX_U8) {
        return pc_gx_fixed_to_float(p[0], attr, type, frac);
    }
    if (type == GX_S8) {
        return pc_gx_fixed_to_float((s8)p[0], attr, type, frac);
    }
    if (type == GX_U16) {
        return pc_gx_fixed_to_float(useNativeLE ? read_le16(p) : read_be16(p), attr, type, frac);
    }
    if (type == GX_S16) {
        return pc_gx_fixed_to_float(useNativeLE ? read_le_s16(p) : read_be_s16(p), attr, type, frac);
    }
    return 0.0f;
}

static int pc_gx_attr_frac(u32 vtxfmt, u32 attr) {
    if (vtxfmt >= PC_GX_MAX_VTXFMT || attr >= PC_GX_MAX_ATTR) {
        return 0;
    }

    PCGXVertexFormat* fmt = &g_gx.vtx_fmt[vtxfmt];
    if (!fmt->valid[attr]) {
        return 0;
    }

    if (attr == GX_VA_POS) {
        return g_gx.pos_frac;
    }
    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
        return g_gx.tc_frac[attr - GX_VA_TEX0];
    }
    if (attr == GX_VA_NRM || attr == GX_VA_NBT) {
        return g_gx.nrm_frac;
    }
    return fmt->frac[attr];
}

static int pc_gx_array_stride(u32 vtxfmt, u32 attr) {
    if (attr >= PC_GX_MAX_ATTR) {
        return 0;
    }

    int stride = g_gx.array_stride[attr];
    if (stride != 0) {
        return stride;
    }
    return pc_gx_attr_size(vtxfmt, attr);
}

static void pc_gx_decode_numeric_attr(const u8* src, u32 vtxfmt, u32 attr, float* out, int max_out) {
    PCGXVertexFormat* fmt = &g_gx.vtx_fmt[vtxfmt];
    int comps = pc_gx_attr_components(attr, fmt->cnt[attr]);
    int size = pc_gx_component_size(fmt->type[attr]);
    int frac = pc_gx_attr_frac(vtxfmt, attr);

    for (int i = 0; i < comps && i < max_out; i++) {
        out[i] = pc_gx_read_numeric_component(src + i * size, attr, fmt->type[attr], frac);
    }
    for (int i = comps; i < max_out; i++) {
        out[i] = 0.0f;
    }
}

static void pc_gx_decode_color_rgba(const u8* src, u32 type, u8* rgba) {
    switch (type) {
    case GX_RGB565: {
        u16 value = read_be16(src);
        rgba[0] = ((value >> 11) & 0x1F) * 255 / 31;
        rgba[1] = ((value >> 5) & 0x3F) * 255 / 63;
        rgba[2] = (value & 0x1F) * 255 / 31;
        rgba[3] = 255;
        break;
    }
    case GX_RGB8:
    case GX_RGBX8:
        rgba[0] = src[0];
        rgba[1] = src[1];
        rgba[2] = src[2];
        rgba[3] = 255;
        break;
    case GX_RGBA4: {
        u16 value = read_be16(src);
        rgba[0] = ((value >> 12) & 0x0F) * 17;
        rgba[1] = ((value >> 8) & 0x0F) * 17;
        rgba[2] = ((value >> 4) & 0x0F) * 17;
        rgba[3] = (value & 0x0F) * 17;
        break;
    }
    case GX_RGBA6:
        rgba[0] = src[0] & 0xFC;
        rgba[1] = ((src[0] << 6) | ((src[1] >> 2) & 0x3C));
        rgba[2] = ((src[1] << 4) | ((src[2] >> 4) & 0x0F));
        rgba[3] = src[2] << 2;
        break;
    case GX_RGBA8:
    default:
        rgba[0] = src[0];
        rgba[1] = src[1];
        rgba[2] = src[2];
        rgba[3] = src[3];
        break;
    }
}

static void pc_gx_store_color_attr(u32 attr, const u8* rgba) {
    if (attr == GX_VA_CLR1) {
        memcpy(g_gx.current_vertex.color1, rgba, 4);
    } else {
        memcpy(g_gx.current_vertex.color0, rgba, 4);
    }
}

static void pc_gx_store_texcoord_attr(u32 attr, float s, float t) {
    int tc = attr - GX_VA_TEX0;
    if (tc < 0 || tc >= 8) {
        return;
    }
    g_gx.current_vertex.texcoord[tc][0] = s;
    g_gx.current_vertex.texcoord[tc][1] = t;
}

static void pc_gx_submit_numeric_attr(u32 attr, const float* values) {
    switch (attr) {
    case GX_VA_POS:
#ifdef TARGET_PC
        if (g_pc_verbose) {
            static int s_pos_decode_log = 0;
            if (s_pos_decode_log < 80 && VIGetRetraceCount() > 850) {
                const u32 fmt = (u32)g_gx.current_vtxfmt;
                u32 posType = (fmt < PC_GX_MAX_VTXFMT) ? g_gx.vtx_fmt[fmt].type[GX_VA_POS] : 0xFFFFFFFFu;
                u32 posCnt = (fmt < PC_GX_MAX_VTXFMT) ? g_gx.vtx_fmt[fmt].cnt[GX_VA_POS] : 0xFFFFFFFFu;
                u32 posFrac = (u32)g_gx.pos_frac;
                fprintf(stderr, "[GX] pos decode: (%.3f, %.3f, %.3f) type=%u cnt=%u frac=%u fmt=%u\n",
                        values[0], values[1], values[2], posType, posCnt, posFrac, fmt);
                fflush(stderr);
                s_pos_decode_log++;
            }
        }
#endif
        GXPosition3f32(values[0], values[1], values[2]);
        break;
    case GX_VA_NRM:
    case GX_VA_NBT:
        GXNormal3f32(values[0], values[1], values[2]);
        break;
    case GX_VA_TEX0:
    case GX_VA_TEX1:
    case GX_VA_TEX2:
    case GX_VA_TEX3:
    case GX_VA_TEX4:
    case GX_VA_TEX5:
    case GX_VA_TEX6:
    case GX_VA_TEX7:
        pc_gx_store_texcoord_attr(attr, values[0], values[1]);
        break;
    default:
        break;
    }
}

static void pc_gx_submit_attr_data(const u8* src, u32 vtxfmt, u32 attr) {
    if (attr == GX_VA_PNMTXIDX || (attr >= GX_VA_TEX0MTXIDX && attr <= GX_VA_TEX7MTXIDX)) {
        pc_gx_set_active_matrix_attr(attr, src[0]);
        return;
    }

    PCGXVertexFormat* fmt = &g_gx.vtx_fmt[vtxfmt];
    if (!fmt->valid[attr]) {
        return;
    }

    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        u8 rgba[4];
        pc_gx_decode_color_rgba(src, fmt->type[attr], rgba);
        pc_gx_store_color_attr(attr, rgba);
        return;
    }

    float values[9];
    pc_gx_decode_numeric_attr(src, vtxfmt, attr, values, 9);
    pc_gx_submit_numeric_attr(attr, values);
}

static void pc_gx_submit_indexed_attr(u32 vtxfmt, u32 attr, u16 idx) {
    const u8* base = (const u8*)g_gx.array_base[attr];
    int stride = pc_gx_array_stride(vtxfmt, attr);
    if (base == NULL || stride <= 0) {
        return;
    }
    if (g_pc_verbose && attr == GX_VA_POS) {
        static bool s_logged_pos_base = false;
        if (!s_logged_pos_base && VIGetRetraceCount() > 850) {
            s_logged_pos_base = true;
            fprintf(stderr, "[GX] POS array base=%p stride=%d count=%u bytes:",
                    (const void*)base, stride, (unsigned)s_array_count_limit[attr]);
            for (int i = 0; i < 24; i++) {
                fprintf(stderr, " %02x", base[i]);
            }
            fprintf(stderr, "\n");
            fflush(stderr);
        }
    }

    u32 countLimit = (attr < PC_GX_MAX_ATTR) ? s_array_count_limit[attr] : 0xFFFFFFFFu;
    if (countLimit != 0 && countLimit != 0xFFFFFFFFu && idx >= countLimit) {
        if (g_pc_verbose) {
            static int s_oob_idx_log = 0;
            if (s_oob_idx_log++ < 64) {
                fprintf(stderr, "[GX] drop oob indexed attr=%u idx=%u count=%u\n",
                        attr, (unsigned)idx, (unsigned)countLimit);
                fflush(stderr);
            }
        }
        return;
    }

    /* Corrupt shape data can provide absurd indices and walk off array bounds. */
    if (idx > 0x7FF || stride > 0x100) {
        if (g_pc_verbose) {
            static int s_bad_idx_log = 0;
            if (s_bad_idx_log++ < 64) {
                fprintf(stderr, "[GX] drop indexed attr=%u idx=%u stride=%d base=%p\n",
                        attr, (unsigned)idx, stride, (const void*)base);
                fflush(stderr);
            }
        }
        return;
    }
    uintptr_t byteOff = (uintptr_t)idx * (uintptr_t)stride;
    if (byteOff > (uintptr_t)(8 * 1024 * 1024)) {
        if (g_pc_verbose) {
            static int s_bad_off_log = 0;
            if (s_bad_off_log++ < 64) {
                fprintf(stderr, "[GX] drop indexed attr=%u off=%lu idx=%u stride=%d\n",
                        attr, (unsigned long)byteOff, (unsigned)idx, stride);
                fflush(stderr);
            }
        }
        return;
    }

    pc_gx_submit_attr_data(base + byteOff, vtxfmt, attr);
}

/* ============================================================
 * Init / Shutdown
 * ============================================================ */
void pc_gx_init(void) {
    pc_platform_ensure_gl_context_current();
    memset(&g_gx, 0, sizeof(g_gx));
    for (u32 i = 0; i < PC_GX_MAX_ATTR; i++) {
        s_array_count_limit[i] = 0xFFFFFFFFu;
    }
    g_gx.dirty = PC_GX_DIRTY_ALL;
    g_gx.viewport[0] = 0.0f;
    g_gx.viewport[1] = 0.0f;
    g_gx.viewport[2] = (float)PC_GC_WIDTH;
    g_gx.viewport[3] = (float)PC_GC_HEIGHT;
    g_gx.viewport[4] = 0.0f;
    g_gx.viewport[5] = 1.0f;
    g_gx.scissor[0] = 0;
    g_gx.scissor[1] = 0;
    g_gx.scissor[2] = PC_GC_WIDTH;
    g_gx.scissor[3] = PC_GC_HEIGHT;
    s_scissor_box_off_x = 0;
    s_scissor_box_off_y = 0;
    g_gx.color_update_enable = 1;
    g_gx.alpha_update_enable = 1;
    g_gx.z_compare_enable = 1;
    g_gx.z_compare_func = GX_LEQUAL;
    g_gx.z_update_enable = 1;
    g_gx.alpha_comp0 = GX_ALWAYS;
    g_gx.alpha_comp1 = GX_ALWAYS;
    g_gx.num_chans = 1;
    g_gx.active_pnmtx = GX_PNMTX0;
    for (int i = 0; i < 8; i++) {
        g_gx.active_texmtx[i] = GX_IDENTITY;
    }

    /* Default swap table: identity */
    for (int i = 0; i < 4; i++) {
        g_gx.tev_swap_table[i].r = 0;
        g_gx.tev_swap_table[i].g = 1;
        g_gx.tev_swap_table[i].b = 2;
        g_gx.tev_swap_table[i].a = 3;
    }

    /* Create GL objects */
    glGenVertexArrays(1, &g_gx.vao);
    glGenBuffers(1, &g_gx.vbo);
    glGenBuffers(1, &g_gx.ebo);
    glBindVertexArray(g_gx.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_gx.vbo);

    /* Set up vertex attributes (persistent layout matching PCGXVertex) */
    /* position: location 0, 3 floats at offset 0 */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PCGXVertex),
                          (void*)offsetof(PCGXVertex, position));
    /* normal: location 1, 3 floats */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PCGXVertex),
                          (void*)offsetof(PCGXVertex, normal));
    /* color0: location 2, 4 ubytes normalized */
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PCGXVertex),
                          (void*)offsetof(PCGXVertex, color0));
    /* texcoord0: location 3, 2 floats (match AC's simpler layout) */
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(PCGXVertex),
                          (void*)offsetof(PCGXVertex, texcoord));

    /* Build quad→triangle EBO: for each quad (0,1,2,3) emit (0,1,2, 0,2,3) */
    for (int q = 0; q < PC_GX_MAX_VERTS / 4; q++) {
        int base = q * 4;
        int idx = q * 6;
        s_quad_indices[idx + 0] = base + 0;
        s_quad_indices[idx + 1] = base + 1;
        s_quad_indices[idx + 2] = base + 2;
        s_quad_indices[idx + 3] = base + 0;
        s_quad_indices[idx + 4] = base + 2;
        s_quad_indices[idx + 5] = base + 3;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gx.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(s_quad_indices), s_quad_indices, GL_STATIC_DRAW);

    /* Build a linear index EBO for non-quad primitives.
     * On macOS/Metal this is more stable than glDrawArrays for dynamic DL draws. */
    for (int i = 0; i < PC_GX_MAX_VERTS; i++) {
        s_linear_indices[i] = (u16)i;
    }
    glGenBuffers(1, &s_linear_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_linear_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(s_linear_indices), s_linear_indices, GL_STATIC_DRAW);

    /* Unbind VAO after init — like AC. Leaving it bound can confuse Metal's
     * state tracking and cause MemoryPoolDecay thread crashes. */
    glBindVertexArray(0);

    glGenTextures(1, &g_gx.fallback_texture);
    glBindTexture(GL_TEXTURE_2D, g_gx.fallback_texture);
    {
        /* Transparent fallback avoids opaque white quads when a texture unit is
         * referenced but its source texture could not be uploaded. */
        static const unsigned char transparent_pixel[4] = {255, 255, 255, 0};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     transparent_pixel);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Default state — match GX defaults (cull none, depth test on) */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);  /* GX default is GX_CULL_NONE */

    pc_gx_tev_init();
    pc_gx_texture_init();

    /* Metal pipeline warmup — pre-compile both simple and full TEV shader
     * pipelines with multiple GL state combinations. Metal compiles pipeline
     * variants lazily and the runtime compilation can crash on macOS ARM64.
     * Pre-compiling at init time when the GL context is clean avoids this. */
    {
        PCGXVertex warmup_verts[4];
        memset(warmup_verts, 0, sizeof(warmup_verts));
        warmup_verts[0].position[0] = -1; warmup_verts[0].position[1] = -1;
        warmup_verts[1].position[0] =  1; warmup_verts[1].position[1] = -1;
        warmup_verts[2].position[0] =  1; warmup_verts[2].position[1] =  1;
        warmup_verts[3].position[0] = -1; warmup_verts[3].position[1] =  1;
        for (int i = 0; i < 4; i++) {
            warmup_verts[i].color0[0] = warmup_verts[i].color0[1] = warmup_verts[i].color0[2] = 0;
            warmup_verts[i].color0[3] = 255;
        }

        glBindVertexArray(g_gx.vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_gx.vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(warmup_verts), warmup_verts, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PCGXVertex),
                              (void*)offsetof(PCGXVertex, position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PCGXVertex),
                              (void*)offsetof(PCGXVertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PCGXVertex),
                              (void*)offsetof(PCGXVertex, color0));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(PCGXVertex),
                              (void*)offsetof(PCGXVertex, texcoord));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gx.ebo);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glViewport(0, 0, g_pc_window_w, g_pc_window_h);
        glDisable(GL_SCISSOR_TEST);

        float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

        /* Warmup each shader with common GL state combos */
        GLuint s_simple = pc_gx_tev_get_simple_shader();
        GLuint s_default = pc_gx_tev_get_default_shader();
        GLuint shaders_to_warmup[2];
        shaders_to_warmup[0] = s_simple;
        shaders_to_warmup[1] = s_default;
        int num_shaders = (s_default != s_simple) ? 2 : 1;

        /* Create a small test texture for warmup */
        GLuint warmup_tex = 0;
        {
            u8 test_pixels[4*4*4];
            memset(test_pixels, 128, sizeof(test_pixels));
            glGenTextures(1, &warmup_tex);
            glBindTexture(GL_TEXTURE_2D, warmup_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, test_pixels);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }

        int warmup_ok = 0, warmup_crash = 0;
        for (int si = 0; si < num_shaders; si++) {
            GLuint sh = shaders_to_warmup[si];
            glUseProgram(sh);
            g_gx.current_shader = sh;
            pc_gx_cache_uniform_locations(sh);
            if (g_gx.uloc.projection >= 0) glUniformMatrix4fv(g_gx.uloc.projection, 1, GL_FALSE, ident);
            if (g_gx.uloc.modelview >= 0)  glUniformMatrix4fv(g_gx.uloc.modelview, 1, GL_FALSE, ident);

            /* Initialize ALL full TEV uniforms to safe defaults so Metal's
             * pipeline compiler doesn't encounter undefined state. */
            if (g_gx.uloc.num_tev_stages >= 0) glUniform1i(g_gx.uloc.num_tev_stages, 1);
            if (g_gx.uloc.alpha_comp0 >= 0) glUniform1i(g_gx.uloc.alpha_comp0, 7); /* ALWAYS */
            if (g_gx.uloc.alpha_comp1 >= 0) glUniform1i(g_gx.uloc.alpha_comp1, 7); /* ALWAYS */
            if (g_gx.uloc.alpha_ref0 >= 0) glUniform1i(g_gx.uloc.alpha_ref0, 0);
            if (g_gx.uloc.alpha_ref1 >= 0) glUniform1i(g_gx.uloc.alpha_ref1, 0);
            if (g_gx.uloc.alpha_op >= 0) glUniform1i(g_gx.uloc.alpha_op, 0);
            if (g_gx.uloc.num_chans >= 0) glUniform1i(g_gx.uloc.num_chans, 1);
            if (g_gx.uloc.lighting_enabled >= 0) glUniform1i(g_gx.uloc.lighting_enabled, 0);
            if (g_gx.uloc.alpha_lighting_enabled >= 0) glUniform1i(g_gx.uloc.alpha_lighting_enabled, 0);
            if (g_gx.uloc.chan_mat_src >= 0) glUniform1i(g_gx.uloc.chan_mat_src, 1); /* VTX */
            if (g_gx.uloc.chan_amb_src >= 0) glUniform1i(g_gx.uloc.chan_amb_src, 0);
            if (g_gx.uloc.alpha_mat_src >= 0) glUniform1i(g_gx.uloc.alpha_mat_src, 1);
            if (g_gx.uloc.light_mask >= 0) glUniform1i(g_gx.uloc.light_mask, 0);
            if (g_gx.uloc.num_ind_stages >= 0) glUniform1i(g_gx.uloc.num_ind_stages, 0);
            if (g_gx.uloc.fog_type >= 0) glUniform1i(g_gx.uloc.fog_type, 0);
            {
                float white4[4] = {1,1,1,1};
                if (g_gx.uloc.tev_prev >= 0) glUniform4fv(g_gx.uloc.tev_prev, 1, white4);
                if (g_gx.uloc.tev_reg0 >= 0) glUniform4fv(g_gx.uloc.tev_reg0, 1, white4);
                if (g_gx.uloc.tev_reg1 >= 0) glUniform4fv(g_gx.uloc.tev_reg1, 1, white4);
                if (g_gx.uloc.tev_reg2 >= 0) glUniform4fv(g_gx.uloc.tev_reg2, 1, white4);
                if (g_gx.uloc.mat_color >= 0) glUniform4fv(g_gx.uloc.mat_color, 1, white4);
                if (g_gx.uloc.amb_color >= 0) glUniform4fv(g_gx.uloc.amb_color, 1, white4);
            }
            /* TEV stage 0: passthrough vertex color (PASSCLR equivalent) */
            if (g_gx.uloc.tev_color_in[0] >= 0) glUniform4i(g_gx.uloc.tev_color_in[0], 15, 15, 15, 10); /* ZERO,ZERO,ZERO,RASC */
            if (g_gx.uloc.tev_alpha_in[0] >= 0) glUniform4i(g_gx.uloc.tev_alpha_in[0], 7, 7, 7, 5);    /* ZERO,ZERO,ZERO,RASA */
            if (g_gx.uloc.tev_color_op[0] >= 0) glUniform1i(g_gx.uloc.tev_color_op[0], 0);
            if (g_gx.uloc.tev_alpha_op[0] >= 0) glUniform1i(g_gx.uloc.tev_alpha_op[0], 0);
            if (g_gx.uloc.tev_bsc[0] >= 0) glUniform4i(g_gx.uloc.tev_bsc[0], 0, 0, 0, 0);
            if (g_gx.uloc.tev_out[0] >= 0) glUniform4i(g_gx.uloc.tev_out[0], 1, 1, 0, 0);
            if (g_gx.uloc.tev_swap[0] >= 0) glUniform2i(g_gx.uloc.tev_swap[0], 0, 0);
            if (g_gx.uloc.tex_map[0] >= 0) glUniform1i(g_gx.uloc.tex_map[0], -1);
            if (g_gx.uloc.tev_tc_src[0] >= 0) glUniform1i(g_gx.uloc.tev_tc_src[0], 0);
            /* Swap table 0: identity */
            if (g_gx.uloc.swap_table >= 0) {
                int sw[16] = {0,1,2,3, 0,1,2,3, 0,1,2,3, 0,1,2,3};
                glUniform4iv(g_gx.uloc.swap_table, 4, sw);
            }
            /* Textures */
            for (int t = 0; t < 8; t++) {
                if (g_gx.uloc.use_texture[t] >= 0) glUniform1i(g_gx.uloc.use_texture[t], 0);
                if (g_gx.uloc.texture[t] >= 0) glUniform1i(g_gx.uloc.texture[t], t);
            }

            /* Crash-protected warmup draw: Metal's pipeline compiler can
             * SIGBUS on new state combinations. Catch and continue. */
            #define WARMUP_DRAW(mode, count, type, offset) do { \
                jmp_buf _wbuf; \
                jmp_buf* _prev = pc_crash_get_jmpbuf(); \
                pc_crash_set_jmpbuf(&_wbuf); \
                if (setjmp(_wbuf) == 0) { \
                    glDrawElements(mode, count, type, offset); \
                    glFinish(); \
                    warmup_ok++; \
                } else { \
                    warmup_crash++; \
                } \
                pc_crash_set_jmpbuf(_prev); \
            } while(0)

            /* State combo 1: blend on, no depth, no texture */
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, g_gx.fallback_texture);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            WARMUP_DRAW(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

            /* State combo 2: depth on, cull on */
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            WARMUP_DRAW(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

            /* State combo 3: no blend, depth on */
            glDisable(GL_BLEND);
            WARMUP_DRAW(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

            /* State combo 4: WITH texture bound */
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, warmup_tex);
            if (g_gx.uloc.use_texture[0] >= 0) glUniform1i(g_gx.uloc.use_texture[0], 1);
            if (g_gx.uloc.tex_map[0] >= 0) glUniform1i(g_gx.uloc.tex_map[0], 0);
            WARMUP_DRAW(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

            /* State combo 5: textured + blend + no depth */
            glEnable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            WARMUP_DRAW(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

            /* State combo 6: textured + scissor */
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, g_pc_window_w, g_pc_window_h);
            WARMUP_DRAW(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            glDisable(GL_SCISSOR_TEST);

            /* State combo 7: triangle strip with linear EBO */
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_linear_ebo);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            WARMUP_DRAW(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);

            /* State combo 8: triangle strip + textured + depth + cull + scissor */
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, g_pc_window_w, g_pc_window_h);
            WARMUP_DRAW(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
            glDisable(GL_SCISSOR_TEST);

            /* State combo 9: no texture, no blend */
            if (g_gx.uloc.use_texture[0] >= 0) glUniform1i(g_gx.uloc.use_texture[0], 0);
            glDisable(GL_BLEND);
            WARMUP_DRAW(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);

            /* State combo 10: no depth, cull on, scissor on, no blend (crash trigger) */
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, g_pc_window_w, g_pc_window_h);
            WARMUP_DRAW(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);

            /* State combo 11: same + blend on */
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            WARMUP_DRAW(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);

            /* State combo 12: no cull, no depth, scissor, blend (second crash state) */
            glDisable(GL_CULL_FACE);
            WARMUP_DRAW(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);

            /* State combo 13: same combos with GL_POINTS (another crash prim) */
            WARMUP_DRAW(GL_POINTS, 4, GL_UNSIGNED_SHORT, 0);
            glDisable(GL_BLEND);
            WARMUP_DRAW(GL_POINTS, 4, GL_UNSIGNED_SHORT, 0);
            glEnable(GL_CULL_FACE);
            WARMUP_DRAW(GL_POINTS, 4, GL_UNSIGNED_SHORT, 0);

            /* State combo 14-15: GL_TRIANGLES with all combos */
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gx.ebo);
            glDisable(GL_CULL_FACE);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_SCISSOR_TEST);
            WARMUP_DRAW(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            glEnable(GL_BLEND);
            WARMUP_DRAW(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

            /* State combo 16-19: GL_TRIANGLES with depth ON (3D scene state) */
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glEnable(GL_BLEND);
            WARMUP_DRAW(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            glDisable(GL_BLEND);
            WARMUP_DRAW(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            WARMUP_DRAW(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            glEnable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            WARMUP_DRAW(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

            #undef WARMUP_DRAW

            /* Unbind texture */
            glDisable(GL_CULL_FACE);
            glDisable(GL_DEPTH_TEST);
            glBindTexture(GL_TEXTURE_2D, 0);
            if (g_gx.uloc.use_texture[0] >= 0) glUniform1i(g_gx.uloc.use_texture[0], 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gx.ebo);
        }
        if (warmup_tex) glDeleteTextures(1, &warmup_tex);

        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Restore the default shader */
        GLuint def = pc_gx_tev_get_shader(&g_gx);
        glUseProgram(def);
        g_gx.current_shader = def;
        pc_gx_cache_uniform_locations(def);

        fprintf(stderr, "[PC] Metal pipeline warmup: %d shaders, %d draws OK, %d draws crashed\n",
                num_shaders, warmup_ok, warmup_crash);
    }

    fprintf(stderr, "[PC] GX initialized (max %d verts, %d TEV stages)\n",
           PC_GX_MAX_VERTS, PC_GX_MAX_TEV_STAGES);
}

void pc_gx_shutdown(void) {
    pc_platform_ensure_gl_context_current();
    pc_gx_tev_shutdown();
    pc_gx_texture_shutdown();
    if (s_boot_simple_vbo) glDeleteBuffers(1, &s_boot_simple_vbo);
    if (s_boot_simple_vao) glDeleteVertexArrays(1, &s_boot_simple_vao);
    if (g_gx.fallback_texture) glDeleteTextures(1, &g_gx.fallback_texture);
    if (g_gx.ebo) glDeleteBuffers(1, &g_gx.ebo);
    if (s_linear_ebo) glDeleteBuffers(1, &s_linear_ebo);
    if (g_gx.vbo) glDeleteBuffers(1, &g_gx.vbo);
    if (g_gx.vao) glDeleteVertexArrays(1, &g_gx.vao);
}

/* ============================================================
 * Uniform location cache
 * ============================================================ */
void pc_gx_cache_uniform_locations(GLuint shader) {
    char name[64];
    g_gx.uloc.projection = glGetUniformLocation(shader, "u_projection");
    g_gx.uloc.modelview = glGetUniformLocation(shader, "u_modelview");
    g_gx.uloc.normal_mtx = glGetUniformLocation(shader, "u_normal_mtx");
    g_gx.uloc.tev_prev = glGetUniformLocation(shader, "u_tev_prev");
    g_gx.uloc.tev_reg0 = glGetUniformLocation(shader, "u_tev_reg0");
    g_gx.uloc.tev_reg1 = glGetUniformLocation(shader, "u_tev_reg1");
    g_gx.uloc.tev_reg2 = glGetUniformLocation(shader, "u_tev_reg2");
    g_gx.uloc.num_tev_stages = glGetUniformLocation(shader, "u_num_tev_stages");
    g_gx.uloc.kcolor = glGetUniformLocation(shader, "u_kcolor[0]");
    g_gx.uloc.alpha_comp0 = glGetUniformLocation(shader, "u_alpha_comp0");
    g_gx.uloc.alpha_ref0 = glGetUniformLocation(shader, "u_alpha_ref0");
    g_gx.uloc.alpha_op = glGetUniformLocation(shader, "u_alpha_op");
    g_gx.uloc.alpha_comp1 = glGetUniformLocation(shader, "u_alpha_comp1");
    g_gx.uloc.alpha_ref1 = glGetUniformLocation(shader, "u_alpha_ref1");
    g_gx.uloc.lighting_enabled = glGetUniformLocation(shader, "u_lighting_enabled");
    g_gx.uloc.mat_color = glGetUniformLocation(shader, "u_mat_color");
    g_gx.uloc.amb_color = glGetUniformLocation(shader, "u_amb_color");
    g_gx.uloc.chan_mat_src = glGetUniformLocation(shader, "u_chan_mat_src");
    g_gx.uloc.chan_amb_src = glGetUniformLocation(shader, "u_chan_amb_src");
    g_gx.uloc.num_chans = glGetUniformLocation(shader, "u_num_chans");
    g_gx.uloc.alpha_lighting_enabled = glGetUniformLocation(shader, "u_alpha_lighting_enabled");
    g_gx.uloc.alpha_mat_src = glGetUniformLocation(shader, "u_alpha_mat_src");
    g_gx.uloc.light_mask = glGetUniformLocation(shader, "u_light_mask");
    g_gx.uloc.num_ind_stages = glGetUniformLocation(shader, "u_num_ind_stages");
    g_gx.uloc.fog_type = glGetUniformLocation(shader, "u_fog_type");
    g_gx.uloc.fog_start = glGetUniformLocation(shader, "u_fog_start");
    g_gx.uloc.fog_end = glGetUniformLocation(shader, "u_fog_end");
    g_gx.uloc.fog_color = glGetUniformLocation(shader, "u_fog_color");
    g_gx.uloc.swap_table = glGetUniformLocation(shader, "u_swap_table[0]");

    for (int i = 0; i < 8; i++) {
        snprintf(name, sizeof(name), "u_light_pos[%d]", i);
        g_gx.uloc.light_pos[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_light_color[%d]", i);
        g_gx.uloc.light_color[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_texmtx%s_enable", i == 0 ? "" : "1");
        g_gx.uloc.texmtx_enable[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_texmtx%s_row0", i == 0 ? "" : "1");
        g_gx.uloc.texmtx_row0[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_texmtx%s_row1", i == 0 ? "" : "1");
        g_gx.uloc.texmtx_row1[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_texgen_src%d", i);
        g_gx.uloc.texgen_src[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_use_texture[%d]", i);
        g_gx.uloc.use_texture[i] = glGetUniformLocation(shader, name);
        if (g_gx.uloc.use_texture[i] < 0) {
            /* Fallback for simple shader which uses u_use_texture0 */
            snprintf(name, sizeof(name), "u_use_texture%d", i);
            g_gx.uloc.use_texture[i] = glGetUniformLocation(shader, name);
        }
        snprintf(name, sizeof(name), "u_texture%d", i);
        g_gx.uloc.texture[i] = glGetUniformLocation(shader, name);
    }
    for (int i = 0; i < 4; i++) {
        snprintf(name, sizeof(name), "u_ind_tex%d", i);
        g_gx.uloc.ind_tex[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_ind_scale[%d]", i);
        g_gx.uloc.ind_scale[i] = glGetUniformLocation(shader, name);
    }
    for (int i = 0; i < PC_GX_MAX_TEV_STAGES; i++) {
        snprintf(name, sizeof(name), "u_tev_color_in[%d]", i);
        g_gx.uloc.tev_color_in[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev_alpha_in[%d]", i);
        g_gx.uloc.tev_alpha_in[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev_color_op[%d]", i);
        g_gx.uloc.tev_color_op[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev_alpha_op[%d]", i);
        g_gx.uloc.tev_alpha_op[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev_tc_src[%d]", i);
        g_gx.uloc.tev_tc_src[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev_bsc[%d]", i);
        g_gx.uloc.tev_bsc[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev_out[%d]", i);
        g_gx.uloc.tev_out[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev_swap[%d]", i);
        g_gx.uloc.tev_swap[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev_ind_cfg[%d]", i);
        g_gx.uloc.tev_ind_cfg[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev_ind_wrap[%d]", i);
        g_gx.uloc.tev_ind_wrap[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev_ksel[%d]", i);
        g_gx.uloc.tev_ksel = (i == 0) ? glGetUniformLocation(shader, name) : g_gx.uloc.tev_ksel;
        snprintf(name, sizeof(name), "u_tex_map[%d]", i);
        g_gx.uloc.tex_map[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_ind_mtx_r0[%d]", i);
        g_gx.uloc.ind_mtx_r0[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_ind_mtx_r1[%d]", i);
        g_gx.uloc.ind_mtx_r1[i] = glGetUniformLocation(shader, name);
    }
    g_gx.uloc.tev_ksel = glGetUniformLocation(shader, "u_tev_ksel[0]");
    g_gx.uloc.simple_kmod = glGetUniformLocation(shader, "u_simple_kmod");
    g_gx.uloc.simple_use_kmod = glGetUniformLocation(shader, "u_simple_use_kmod");
}

static void pc_simple_shader_kmod(const PCGXState* state, float out_kmod[4], int* out_use) {
    out_kmod[0] = 1.0f;
    out_kmod[1] = 1.0f;
    out_kmod[2] = 1.0f;
    out_kmod[3] = 1.0f;
    *out_use = 0;

    int sel = state->tev_stages[0].k_color_sel;
    if (g_pc_verbose) {
        static int s_kmod_log = 0;
        if (s_kmod_log < 30) {
            fprintf(stderr, "[TEV-KMOD] kcsel=%d tex=%d cin=(%d,%d,%d,%d) C0=(%.2f,%.2f,%.2f) K0=(%.2f,%.2f,%.2f)\n",
                    sel, state->tev_stages[0].tex_map,
                    state->tev_stages[0].color_a, state->tev_stages[0].color_b,
                    state->tev_stages[0].color_c, state->tev_stages[0].color_d,
                    state->tev_colors[0][0], state->tev_colors[0][1], state->tev_colors[0][2],
                    state->tev_k_colors[0][0], state->tev_k_colors[0][1], state->tev_k_colors[0][2]);
            s_kmod_log++;
        }
    }
    if (sel <= GX_TEV_KCSEL_1_8) {
        return;
    }

    if (sel >= GX_TEV_KCSEL_K0 && sel <= GX_TEV_KCSEL_K3) {
        int idx = sel - GX_TEV_KCSEL_K0;
        out_kmod[0] = state->tev_k_colors[idx][0];
        out_kmod[1] = state->tev_k_colors[idx][1];
        out_kmod[2] = state->tev_k_colors[idx][2];
        *out_use = 1;
        return;
    }

    if (sel >= GX_TEV_KCSEL_K0_R && sel <= GX_TEV_KCSEL_K3_A) {
        int idx = (sel - GX_TEV_KCSEL_K0_R) / 4;
        int comp = (sel - GX_TEV_KCSEL_K0_R) % 4;
        float v = state->tev_k_colors[idx][comp];
        out_kmod[0] = v;
        out_kmod[1] = v;
        out_kmod[2] = v;
        *out_use = 1;
        return;
    }
}

/* ============================================================
 * GL state application helpers
 * ============================================================ */
static GLenum gl_blend_factor(int gx_factor) {
    switch (gx_factor) {
        case GX_BL_ZERO:        return GL_ZERO;
        case GX_BL_ONE:         return GL_ONE;
        case GX_BL_SRCCLR:      return GL_SRC_COLOR;
        case GX_BL_INVSRCCLR:   return GL_ONE_MINUS_SRC_COLOR;
        case GX_BL_SRCALPHA:    return GL_SRC_ALPHA;
        case GX_BL_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
        case GX_BL_DSTALPHA:    return GL_DST_ALPHA;
        case GX_BL_INVDSTALPHA: return GL_ONE_MINUS_DST_ALPHA;
        default:                return GL_ONE;
    }
}

static GLenum gl_compare_func(int gx_func) {
    switch (gx_func) {
        case GX_NEVER:   return GL_NEVER;
        case GX_LESS:    return GL_LESS;
        case GX_EQUAL:   return GL_EQUAL;
        case GX_LEQUAL:  return GL_LEQUAL;
        case GX_GREATER: return GL_GREATER;
        case GX_NEQUAL:  return GL_NOTEQUAL;
        case GX_GEQUAL:  return GL_GEQUAL;
        case GX_ALWAYS:  return GL_ALWAYS;
        default:         return GL_LEQUAL;
    }
}

static void apply_gl_state(void) {
    /* Blend */
    if (g_gx.dirty & PC_GX_DIRTY_BLEND) {
        if (g_gx.blend_mode == GX_BM_BLEND) {
            glEnable(GL_BLEND);
            glBlendFunc(gl_blend_factor(g_gx.blend_src), gl_blend_factor(g_gx.blend_dst));
            glBlendEquation(GL_FUNC_ADD); /* Reset in case previous mode was SUBTRACT */
        } else if (g_gx.blend_mode == GX_BM_SUBTRACT) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        } else {
            glDisable(GL_BLEND);
        }
    }

    /* Depth */
    if (g_gx.dirty & PC_GX_DIRTY_DEPTH) {
        if (g_gx.z_compare_enable) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(gl_compare_func(g_gx.z_compare_func));
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(g_gx.z_update_enable ? GL_TRUE : GL_FALSE);
    }

    /* Color mask */
    if (g_gx.dirty & PC_GX_DIRTY_COLOR_MASK) {
        /* On macOS ARM64, certain glColorMask combinations crash Apple's Metal
         * shader compiler. Always enable all color writes to avoid this.
         * TODO: investigate which specific mask combinations are safe. */
#ifdef __APPLE__
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
#else
        glColorMask(g_gx.color_update_enable ? GL_TRUE : GL_FALSE,
                    g_gx.color_update_enable ? GL_TRUE : GL_FALSE,
                    g_gx.color_update_enable ? GL_TRUE : GL_FALSE,
                    g_gx.alpha_update_enable ? GL_TRUE : GL_FALSE);
#endif
    }

    /* Cull mode - GX uses opposite winding from GL */
    if (g_gx.dirty & PC_GX_DIRTY_CULL) {
        switch (g_gx.cull_mode) {
            case GX_CULL_NONE:  glDisable(GL_CULL_FACE); break;
            case GX_CULL_FRONT: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); break;
            case GX_CULL_BACK:  glEnable(GL_CULL_FACE); glCullFace(GL_BACK); break;
            case GX_CULL_ALL:   glEnable(GL_CULL_FACE); glCullFace(GL_FRONT_AND_BACK); break;
        }
    }
}

/* Convert GX 3x4 row-major matrix to GL 4x4 column-major */
static void mtx34_to_gl44(const float src[3][4], float dst[16]) {
    dst[ 0] = src[0][0]; dst[ 1] = src[1][0]; dst[ 2] = src[2][0]; dst[ 3] = 0.0f;
    dst[ 4] = src[0][1]; dst[ 5] = src[1][1]; dst[ 6] = src[2][1]; dst[ 7] = 0.0f;
    dst[ 8] = src[0][2]; dst[ 9] = src[1][2]; dst[10] = src[2][2]; dst[11] = 0.0f;
    dst[12] = src[0][3]; dst[13] = src[1][3]; dst[14] = src[2][3]; dst[15] = 1.0f;
}

/* Convert GX projection to GL 4x4 column-major.
 * GX maps Z to NDC [0,1]; OpenGL 3.3 maps Z to NDC [-1,1].
 * Remap: z_gl = 2*z_gx - 1, so Z coefficients must be adjusted:
 *   perspective: dst[10] = 2*src[2][2] + 1, dst[14] = 2*src[2][3]
 *   orthographic: dst[10] = 2*src[2][2], dst[14] = 2*src[2][3] - 1 */
static void proj_to_gl44(const float src[4][4], int type, float dst[16]) {
    if (type == GX_PERSPECTIVE) {
        dst[ 0] = src[0][0]; dst[ 1] = 0;          dst[ 2] = 0;                     dst[ 3] = 0;
        dst[ 4] = 0;         dst[ 5] = src[1][1];   dst[ 6] = 0;                     dst[ 7] = 0;
        dst[ 8] = src[0][2]; dst[ 9] = src[1][2];   dst[10] = 2*src[2][2] + 1.0f;    dst[11] = -1.0f;
        dst[12] = 0;         dst[13] = 0;           dst[14] = 2*src[2][3];            dst[15] = 0;
    } else {
        /* GX orthographic */
        dst[ 0] = src[0][0]; dst[ 1] = 0;          dst[ 2] = 0;                     dst[ 3] = 0;
        dst[ 4] = 0;         dst[ 5] = src[1][1];   dst[ 6] = 0;                     dst[ 7] = 0;
        dst[ 8] = 0;         dst[ 9] = 0;           dst[10] = 2*src[2][2];            dst[11] = 0;
        dst[12] = src[0][3]; dst[13] = src[1][3];   dst[14] = 2*src[2][3] - 1.0f;    dst[15] = 1.0f;
    }
}

static void upload_uniforms(void) {
    unsigned int d = g_gx.dirty;

    /* Use snapshotted TEV state from GXBegin time if available.
     * This matches GCN hardware where TEV state is latched at draw time,
     * not at flush time (which may be after cleanup code has run). */
    const int snap = g_gx.snap_valid;
    const int num_tev = snap ? g_gx.snap_num_tev_stages : g_gx.num_tev_stages;
    const PCGXTevStage* tev = snap ? g_gx.snap_tev_stages : g_gx.tev_stages;
    const int num_tgens = snap ? g_gx.snap_num_tex_gens : g_gx.num_tex_gens;
    const int num_ch = snap ? g_gx.snap_num_chans : g_gx.num_chans;

    const int shader_tev_stage_cap = PC_GX_MAX_TEV_STAGES;
    const int shader_tev_stage_count =
        (num_tev < shader_tev_stage_cap) ? num_tev : shader_tev_stage_cap;


    if (d & PC_GX_DIRTY_PROJECTION) {
        float gl_proj[16];
        proj_to_gl44(g_gx.projection_mtx, g_gx.projection_type, gl_proj);
        if (g_gx.uloc.projection >= 0)
            glUniformMatrix4fv(g_gx.uloc.projection, 1, GL_FALSE, gl_proj);
    }

    if (d & PC_GX_DIRTY_MODELVIEW) {
        float gl_mv[16];
        if (g_gx.batch_pretransformed) {
            static const float ident44[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
            };
            memcpy(gl_mv, ident44, sizeof(gl_mv));
        } else if (snap) {
            /* Use snapshotted modelview from GXBegin time — matches GCN
             * behavior where the position matrix is latched at draw time.
             * J2DPicture cleanup code can overwrite pos_mtx between
             * GXBegin and the deferred auto-flush. */
            mtx34_to_gl44(g_gx.snap_pos_mtx, gl_mv);
        } else {
            int idx = g_gx.current_mtx / 3;
            if (idx < 0 || idx >= 10) idx = 0;
            mtx34_to_gl44(g_gx.pos_mtx[idx], gl_mv);
        }
        if (g_gx.uloc.modelview >= 0)
            glUniformMatrix4fv(g_gx.uloc.modelview, 1, GL_FALSE, gl_mv);

        float nrm[9];
        if (g_gx.batch_pretransformed) {
            nrm[0] = 1.0f; nrm[1] = 0.0f; nrm[2] = 0.0f;
            nrm[3] = 0.0f; nrm[4] = 1.0f; nrm[5] = 0.0f;
            nrm[6] = 0.0f; nrm[7] = 0.0f; nrm[8] = 1.0f;
        } else {
            int nidx = snap ? (g_gx.snap_current_mtx / 3) : (g_gx.current_mtx / 3);
            if (nidx < 0 || nidx >= 10) nidx = 0;
            nrm[0] = g_gx.nrm_mtx[nidx][0][0]; nrm[1] = g_gx.nrm_mtx[nidx][1][0]; nrm[2] = g_gx.nrm_mtx[nidx][2][0];
            nrm[3] = g_gx.nrm_mtx[nidx][0][1]; nrm[4] = g_gx.nrm_mtx[nidx][1][1]; nrm[5] = g_gx.nrm_mtx[nidx][2][1];
            nrm[6] = g_gx.nrm_mtx[nidx][0][2]; nrm[7] = g_gx.nrm_mtx[nidx][1][2]; nrm[8] = g_gx.nrm_mtx[nidx][2][2];
        }
        if (g_gx.uloc.normal_mtx >= 0)
            glUniformMatrix3fv(g_gx.uloc.normal_mtx, 1, GL_FALSE, nrm);
    }

    if (d & PC_GX_DIRTY_TEV_COLORS) {
        if (g_gx.uloc.tev_prev >= 0) glUniform4fv(g_gx.uloc.tev_prev, 1, g_gx.tev_colors[0]);
        if (g_gx.uloc.tev_reg0 >= 0) glUniform4fv(g_gx.uloc.tev_reg0, 1, g_gx.tev_colors[1]);
        if (g_gx.uloc.tev_reg1 >= 0) glUniform4fv(g_gx.uloc.tev_reg1, 1, g_gx.tev_colors[2]);
        if (g_gx.uloc.tev_reg2 >= 0) glUniform4fv(g_gx.uloc.tev_reg2, 1, g_gx.tev_colors[3]);
    }

    if (d & PC_GX_DIRTY_TEV_STAGES) {
        if (g_gx.uloc.num_tev_stages >= 0)
            glUniform1i(g_gx.uloc.num_tev_stages, shader_tev_stage_count);

        for (int i = 0; i < shader_tev_stage_count; i++) {
            const PCGXTevStage* s = &tev[i];
            if (g_gx.uloc.tev_color_in[i] >= 0)
                glUniform4i(g_gx.uloc.tev_color_in[i], s->color_a, s->color_b, s->color_c, s->color_d);
            if (g_gx.uloc.tev_alpha_in[i] >= 0)
                glUniform4i(g_gx.uloc.tev_alpha_in[i], s->alpha_a, s->alpha_b, s->alpha_c, s->alpha_d);
            if (g_gx.uloc.tev_color_op[i] >= 0)
                glUniform1i(g_gx.uloc.tev_color_op[i], s->color_op);
            if (g_gx.uloc.tev_alpha_op[i] >= 0)
                glUniform1i(g_gx.uloc.tev_alpha_op[i], s->alpha_op);
            if (g_gx.uloc.tev_tc_src[i] >= 0)
                glUniform1i(g_gx.uloc.tev_tc_src[i], s->tex_coord);
            if (g_gx.uloc.tev_bsc[i] >= 0)
                glUniform4i(g_gx.uloc.tev_bsc[i], s->color_bias, s->color_scale, s->alpha_bias, s->alpha_scale);
            if (g_gx.uloc.tev_out[i] >= 0)
                glUniform4i(g_gx.uloc.tev_out[i], s->color_clamp, s->alpha_clamp, s->color_out, s->alpha_out);
        }
    }

    if (d & PC_GX_DIRTY_SWAP_TABLES) {
        if (g_gx.uloc.swap_table >= 0) {
            int sw[16];
            for (int i = 0; i < 4; i++) {
                sw[i * 4 + 0] = g_gx.tev_swap_table[i].r;
                sw[i * 4 + 1] = g_gx.tev_swap_table[i].g;
                sw[i * 4 + 2] = g_gx.tev_swap_table[i].b;
                sw[i * 4 + 3] = g_gx.tev_swap_table[i].a;
            }
            glUniform4iv(g_gx.uloc.swap_table, 4, sw);
        }
        for (int i = 0; i < shader_tev_stage_count; i++) {
            if (g_gx.uloc.tev_swap[i] >= 0)
                glUniform2i(g_gx.uloc.tev_swap[i], tev[i].ras_swap, tev[i].tex_swap);
        }
    }

    if (d & PC_GX_DIRTY_KONST) {
        if (g_gx.uloc.kcolor >= 0)
            glUniform4fv(g_gx.uloc.kcolor, 4, (float*)g_gx.tev_k_colors);
        if (g_gx.uloc.tev_ksel >= 0) {
            int ksel[PC_GX_MAX_TEV_STAGES * 3];
            memset(ksel, 0, sizeof(ksel));
            for (int i = 0; i < shader_tev_stage_count; i++) {
                ksel[i * 3 + 0] = tev[i].k_color_sel;
                ksel[i * 3 + 1] = tev[i].k_alpha_sel;
                ksel[i * 3 + 2] = 0;
            }
            glUniform3iv(g_gx.uloc.tev_ksel, shader_tev_stage_count > 0 ? shader_tev_stage_count : 1, ksel);
        }
    }

    if (d & PC_GX_DIRTY_ALPHA_CMP) {
        if (g_gx.uloc.alpha_comp0 >= 0) glUniform1i(g_gx.uloc.alpha_comp0, snap ? g_gx.snap_alpha_comp0 : g_gx.alpha_comp0);
        if (g_gx.uloc.alpha_ref0 >= 0) glUniform1i(g_gx.uloc.alpha_ref0, snap ? g_gx.snap_alpha_ref0 : g_gx.alpha_ref0);
        if (g_gx.uloc.alpha_op >= 0) glUniform1i(g_gx.uloc.alpha_op, snap ? g_gx.snap_alpha_op : g_gx.alpha_op);
        if (g_gx.uloc.alpha_comp1 >= 0) glUniform1i(g_gx.uloc.alpha_comp1, snap ? g_gx.snap_alpha_comp1 : g_gx.alpha_comp1);
        if (g_gx.uloc.alpha_ref1 >= 0) glUniform1i(g_gx.uloc.alpha_ref1, snap ? g_gx.snap_alpha_ref1 : g_gx.alpha_ref1);
    }

    if (d & PC_GX_DIRTY_LIGHTING) {
        int lit = g_gx.chan_ctrl_enable[0];
        int alit = (g_gx.num_chans > 0) ? g_gx.chan_ctrl_enable[1] : 0;
        if (g_gx.uloc.lighting_enabled >= 0) glUniform1i(g_gx.uloc.lighting_enabled, lit);
        if (g_gx.uloc.alpha_lighting_enabled >= 0) glUniform1i(g_gx.uloc.alpha_lighting_enabled, alit);
        if (g_gx.uloc.num_chans >= 0) glUniform1i(g_gx.uloc.num_chans, num_ch);
        if (g_gx.uloc.mat_color >= 0) glUniform4fv(g_gx.uloc.mat_color, 1, g_gx.chan_mat_color[0]);
        if (g_gx.uloc.amb_color >= 0) glUniform4fv(g_gx.uloc.amb_color, 1, g_gx.chan_amb_color[0]);
        if (g_gx.uloc.chan_mat_src >= 0) glUniform1i(g_gx.uloc.chan_mat_src, g_gx.chan_ctrl_mat_src[0]);
        if (g_gx.uloc.chan_amb_src >= 0) glUniform1i(g_gx.uloc.chan_amb_src, g_gx.chan_ctrl_amb_src[0]);
        if (g_gx.uloc.alpha_mat_src >= 0) glUniform1i(g_gx.uloc.alpha_mat_src, g_gx.chan_ctrl_mat_src[1]);
        if (g_gx.uloc.light_mask >= 0) glUniform1i(g_gx.uloc.light_mask, g_gx.chan_ctrl_light_mask[0]);

        for (int i = 0; i < 8; i++) {
            if (g_gx.uloc.light_pos[i] >= 0)
                glUniform3fv(g_gx.uloc.light_pos[i], 1, g_gx.lights[i].pos);
            if (g_gx.uloc.light_color[i] >= 0)
                glUniform4fv(g_gx.uloc.light_color[i], 1, g_gx.lights[i].color);
        }
    }

    if (d & PC_GX_DIRTY_TEXGEN) {
        for (int i = 0; i < 2; i++) {
            if (g_gx.uloc.texgen_src[i] >= 0)
                glUniform1i(g_gx.uloc.texgen_src[i], g_gx.tex_gen_src[i]);
            /* Texture matrix: check if the gen uses a matrix (GX_TEXMTX0=30..GX_TEXMTX9=57, step 3) */
            int mtx_idx = (g_gx.tex_gen_mtx[i] - 30) / 3;
            int has_mtx = (mtx_idx >= 0 && mtx_idx < 10 && g_gx.tex_gen_mtx[i] != 60 /* GX_IDENTITY */);
            if (g_gx.uloc.texmtx_enable[i] >= 0)
                glUniform1i(g_gx.uloc.texmtx_enable[i], has_mtx ? 1 : 0);
            if (has_mtx) {
                if (g_gx.uloc.texmtx_row0[i] >= 0)
                    glUniform4fv(g_gx.uloc.texmtx_row0[i], 1, g_gx.tex_mtx[mtx_idx][0]);
                if (g_gx.uloc.texmtx_row1[i] >= 0)
                    glUniform4fv(g_gx.uloc.texmtx_row1[i], 1, g_gx.tex_mtx[mtx_idx][1]);
            }
        }
    }

    if (d & (PC_GX_DIRTY_TEXTURES | PC_GX_DIRTY_TEV_STAGES)) {
        GLuint fallback_tex = g_gx.fallback_texture;

        /* Bind all 8 texture maps to their GL texture units */
        int use_tex[8] = {0,0,0,0,0,0,0,0};
        for (int i = 0; i < 8; i++) {
            GLuint tex = g_gx.gl_textures[i];
            use_tex[i] = (tex != 0) ? 1 : 0;
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, tex ? tex : fallback_tex);
        }

        /* Upload u_use_texture[0..7] and u_texture[0..7] */
        for (int i = 0; i < 8; i++) {
            if (g_gx.uloc.use_texture[i] >= 0)
                glUniform1i(g_gx.uloc.use_texture[i], use_tex[i]);
            if (g_gx.uloc.texture[i] >= 0)
                glUniform1i(g_gx.uloc.texture[i], i);
        }

        /* Upload per-stage tex_map using snapshotted TEV state */
        for (int i = 0; i < shader_tev_stage_count; i++) {
            int tm = tev[i].tex_map;
            if ((tm < 0 || tm >= 8) && num_tgens > 0) {
                /* Infer from loaded textures: use the stage index as the map */
                if (i < 8 && g_gx.gl_textures[i] != 0) {
                    tm = i;
                } else if (s_last_loaded_tex_map >= 0 && s_last_loaded_tex_map < 8 &&
                           g_gx.gl_textures[s_last_loaded_tex_map] != 0) {
                    tm = s_last_loaded_tex_map;
                }
            }
            if (g_gx.uloc.tex_map[i] >= 0)
                glUniform1i(g_gx.uloc.tex_map[i], (tm >= 0 && tm < 8) ? tm : -1);
        }

        /* Indirect textures on units 8-9 */
        if (g_gx.uloc.ind_tex[0] >= 0) glUniform1i(g_gx.uloc.ind_tex[0], 8);
        if (g_gx.uloc.ind_tex[1] >= 0) glUniform1i(g_gx.uloc.ind_tex[1], 9);
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, fallback_tex);
        glActiveTexture(GL_TEXTURE9);
        glBindTexture(GL_TEXTURE_2D, fallback_tex);
        glActiveTexture(GL_TEXTURE0);
    }

    if (d & PC_GX_DIRTY_INDIRECT) {
        if (g_gx.uloc.num_ind_stages >= 0)
            glUniform1i(g_gx.uloc.num_ind_stages, g_gx.num_ind_stages);
        for (int i = 0; i < shader_tev_stage_count; i++) {
            const PCGXTevStage* s = &tev[i];
            if (g_gx.uloc.tev_ind_cfg[i] >= 0)
                glUniform4i(g_gx.uloc.tev_ind_cfg[i], s->ind_stage, s->ind_mtx, s->ind_bias, s->ind_alpha);
            if (g_gx.uloc.tev_ind_wrap[i] >= 0)
                glUniform3i(g_gx.uloc.tev_ind_wrap[i], s->ind_wrap_s, s->ind_wrap_t, s->ind_add_prev);
        }
    }

    if (d & PC_GX_DIRTY_FOG) {
        if (g_gx.uloc.fog_type >= 0)  glUniform1i(g_gx.uloc.fog_type, g_gx.fog_type);
        if (g_gx.uloc.fog_start >= 0) glUniform1f(g_gx.uloc.fog_start, g_gx.fog_start);
        if (g_gx.uloc.fog_end >= 0)   glUniform1f(g_gx.uloc.fog_end, g_gx.fog_end);
        if (g_gx.uloc.fog_color >= 0) glUniform4fv(g_gx.uloc.fog_color, 1, g_gx.fog_color);
    }

    /* Debug: log GX state for first few draw calls of scene frames */
    {
        static int s_uniform_log = 0;
        static int s_prev_tev = -1;
        u32 r = VIGetRetraceCount();
        /* Log when TEV stage count changes or first few draws */
        if (r > 500 && (s_uniform_log < 15 || (num_tev != s_prev_tev && s_uniform_log < 50))) {
            const PCGXTevStage* s0 = &tev[0];
            fprintf(stderr, "[GX-STATE] tev=%d chans=%d lit=%d matSrc=%d ambSrc=%d "
                    "mat=(%.2f,%.2f,%.2f) amb=(%.2f,%.2f,%.2f) "
                    "s0=[cIn=%d,%d,%d,%d aIn=%d,%d,%d,%d tm=%d tc=%d] "
                    "fog=%d tex0=%u lmask=0x%x prev=(%.2f,%.2f,%.2f)\n",
                    num_tev, num_ch, g_gx.chan_ctrl_enable[0],
                    g_gx.chan_ctrl_mat_src[0], g_gx.chan_ctrl_amb_src[0],
                    g_gx.chan_mat_color[0][0], g_gx.chan_mat_color[0][1], g_gx.chan_mat_color[0][2],
                    g_gx.chan_amb_color[0][0], g_gx.chan_amb_color[0][1], g_gx.chan_amb_color[0][2],
                    s0->color_a, s0->color_b, s0->color_c, s0->color_d,
                    s0->alpha_a, s0->alpha_b, s0->alpha_c, s0->alpha_d,
                    s0->tex_map, s0->tex_coord,
                    g_gx.fog_type, g_gx.gl_textures[0], g_gx.chan_ctrl_light_mask[0],
                    g_gx.tev_colors[0][0], g_gx.tev_colors[0][1], g_gx.tev_colors[0][2]);
            s_prev_tev = num_tev;
            s_uniform_log++;
        }
    }
}

/* ============================================================
 * Vertex flush — the core rendering function
 * ============================================================ */
static int s_draw_call_count = 0;

static bool is_redundant_boot_clear_quad(const PCGXVertex* verts, int count) {
    if (count != 4) {
        return false;
    }

    static const float expected_pos[4][2] = {
        {0.0f, 0.0f},
        {(float)PC_GC_WIDTH, 0.0f},
        {(float)PC_GC_WIDTH, (float)PC_GC_HEIGHT},
        {0.0f, (float)PC_GC_HEIGHT},
    };

    for (int i = 0; i < 4; i++) {
        if (fabsf(verts[i].position[0] - expected_pos[i][0]) > 0.01f ||
            fabsf(verts[i].position[1] - expected_pos[i][1]) > 0.01f ||
            fabsf(verts[i].position[2]) > 0.01f) {
            return false;
        }
        if (verts[i].color0[0] != 0 || verts[i].color0[1] != 0 ||
            verts[i].color0[2] != 0 || verts[i].color0[3] != 255) {
            return false;
        }
    }

    return true;
}

static void ensure_boot_simple_objects(void) {
    if (s_boot_simple_vao != 0 && s_boot_simple_vbo != 0) {
        return;
    }

    glGenVertexArrays(1, &s_boot_simple_vao);
    glGenBuffers(1, &s_boot_simple_vbo);
    glBindVertexArray(s_boot_simple_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_boot_simple_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PCBootSimpleVertex),
                          (void*)offsetof(PCBootSimpleVertex, position));
    glDisableVertexAttribArray(1);
    glVertexAttrib3f(1, 0.0f, 0.0f, 0.0f);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PCBootSimpleVertex),
                          (void*)offsetof(PCBootSimpleVertex, color0));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(PCBootSimpleVertex),
                          (void*)offsetof(PCBootSimpleVertex, texcoord));
    glBindVertexArray(0);
}

static bool draw_boot_simple_quad(const PCGXVertex* verts, int count) {
    if (count != 4) {
        return false;
    }

    ensure_boot_simple_objects();

    PCBootSimpleVertex boot_verts[4];
    for (int i = 0; i < 4; i++) {
        memcpy(boot_verts[i].position, verts[i].position, sizeof(boot_verts[i].position));
        memcpy(boot_verts[i].color0, verts[i].color0, sizeof(boot_verts[i].color0));
        boot_verts[i].texcoord[0] = verts[i].texcoord[0][0];
        boot_verts[i].texcoord[1] = verts[i].texcoord[0][1];
    }

    glBindVertexArray(s_boot_simple_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_boot_simple_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(boot_verts), boot_verts, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(g_gx.vao);
    return true;
}

static int s_frame_draws_ok = 0, s_frame_draws_crash = 0, s_frame_draws_total = 0;
static u32 s_last_frame_retrace = 0;
void pc_gx_flush_vertices(void) {
    pc_platform_ensure_gl_context_current();
    int count = g_gx.current_vertex_idx;
    if (count <= 0) {
        g_gx.vertex_count = 0;
        g_gx.current_vertex_idx = 0;
        g_gx.batch_pretransformed = 0;
        return;
    }
    /* Per-frame draw stats */
    {
        u32 r = VIGetRetraceCount();
        if (r != s_last_frame_retrace) {
            static int s_stat_log = 0;
            if (s_stat_log < 10 && r > 700) {
                fprintf(stderr, "[DRAW-STATS] frame %u: %d ok, %d crashed, %d total (pretrans=%d)\n",
                        s_last_frame_retrace, s_frame_draws_ok, s_frame_draws_crash, s_frame_draws_total,
                        g_gx.batch_pretransformed);
                s_stat_log++;
            }
            s_frame_draws_ok = 0;
            s_frame_draws_crash = 0;
            s_frame_draws_total = 0;
            s_last_frame_retrace = r;
        }
        s_frame_draws_total++;
    }
    s_draw_call_count++;

    /* Force ortho projection for 2D quads: if all vertices have z=0 and the
     * projection is perspective, the w=0 divide produces undefined results.
     * This happens during logo/boot screens where the Painter's 3D camera setup
     * leaves a stale perspective projection. */
    if (g_gx.projection_type == GX_PERSPECTIVE && count >= 4 &&
        g_gx.current_primitive == GX_QUADS) {
        bool all_z0 = true;
        for (int i = 0; i < count && i < 4; i++) {
            if (g_gx.vertex_buffer[i].position[2] != 0.0f) {
                all_z0 = false;
                break;
            }
        }
        if (all_z0) {
            /* Override to orthographic projection for 608×448 framebuffer.
             * GC ortho maps (0,0)=top-left, (608,448)=bottom-right.
             * GL NDC has Y-up, so negate Y scale to flip. */
            float ortho[4][4];
            memset(ortho, 0, sizeof(ortho));
            ortho[0][0] = 2.0f / 608.0f;
            ortho[0][3] = -1.0f;
            ortho[1][1] = -2.0f / 448.0f;
            ortho[1][3] = 1.0f;
            ortho[2][2] = -1.0f;
            ortho[2][3] = 0.0f;
            memcpy(g_gx.projection_mtx, ortho, sizeof(ortho));
            g_gx.projection_type = GX_ORTHOGRAPHIC;
            /* Force all dirty flags so projection, modelview, and TEV
             * are all re-uploaded for this draw */
            g_gx.dirty = 0xFFFFFFFF;
            /* Disable back-face culling: the Y-flip in the ortho matrix
             * reverses winding order, causing CW quads to be culled */
            g_gx.cull_mode = GX_CULL_NONE;
            static int s_ortho_override_log = 0;
            if (s_ortho_override_log++ < 3) {
                fprintf(stderr, "[GX] 2D ortho override: texW=%d verts=%d\n",
                        g_gx.tex_obj_w[0], count);
            }
        }
    }

/* Select shader and cache uniforms if changed */
    GLuint shader = pc_gx_tev_get_shader(&g_gx);
    if (shader != g_gx.current_shader) {
        glUseProgram(shader);
        g_gx.current_shader = shader;
        pc_gx_cache_uniform_locations(shader);
    }
    /* Only upload changed state — full uploads trigger Metal compiler crashes */

    apply_gl_state();
    if (pc_gx_tev_is_simple_shader(shader)) {
        /* Keep a stable GL pipeline state for simple-shader fallback on macOS. */
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    /* For simple shader, only upload projection/modelview — skip full TEV uniforms.
     * Uploading uniforms to non-existent locations is harmless but may trigger
     * Metal state tracking bugs. */
    if (pc_gx_tev_is_simple_shader(shader)) {
        /* Minimal uniforms for simple shader */
        float gl_proj[16];
        proj_to_gl44(g_gx.projection_mtx, g_gx.projection_type, gl_proj);
        if (g_gx.uloc.projection >= 0)
            glUniformMatrix4fv(g_gx.uloc.projection, 1, GL_FALSE, gl_proj);
        float gl_mv[16];
        if (g_gx.batch_pretransformed) {
            static const float ident44[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
            };
            memcpy(gl_mv, ident44, sizeof(gl_mv));
        } else {
            int idx = g_gx.current_mtx / 3;
            if (idx < 0 || idx >= 10) idx = 0;
            mtx34_to_gl44(g_gx.pos_mtx[idx], gl_mv);
        }
        if (g_gx.uloc.modelview >= 0)
            glUniformMatrix4fv(g_gx.uloc.modelview, 1, GL_FALSE, gl_mv);
        /* Texture setup for simple shader */
        GLuint fallback_tex = g_gx.fallback_texture ? g_gx.fallback_texture : g_gx.gl_textures[0];
        int tex_map = g_gx.tev_stages[0].tex_map;
        GLuint tex0 = 0;
        if (tex_map >= 0 && tex_map < 8) {
            tex0 = g_gx.gl_textures[tex_map];
        }
        if (tex0 == 0 && s_last_loaded_tex_map >= 0 && s_last_loaded_tex_map < 8) {
            tex0 = g_gx.gl_textures[s_last_loaded_tex_map];
        }
        if (tex0 == 0) {
            for (int i = 0; i < 8; i++) {
                if (g_gx.gl_textures[i] != 0) {
                    tex0 = g_gx.gl_textures[i];
                    break;
                }
            }
        }
        /* If TEV_ORDER says no texture but we DO have a texture in map 0
         * (from J3D fallback loading), use it anyway. The byte-swap corruption
         * makes TEV_ORDER disable textures incorrectly. */
        if (tex0 == 0 && g_gx.gl_textures[0] != 0) {
            tex0 = g_gx.gl_textures[0];
        }
        int use_tex0 = (tex0 != 0) ? 1 : 0;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, use_tex0 ? tex0 : fallback_tex);
        if (g_gx.uloc.use_texture[0] >= 0)
            glUniform1i(g_gx.uloc.use_texture[0], use_tex0);
        if (g_gx.uloc.texture[0] >= 0)
            glUniform1i(g_gx.uloc.texture[0], 0);
        float kmod[4];
        int use_kmod = 0;
        pc_simple_shader_kmod(&g_gx, kmod, &use_kmod);
        if (g_gx.uloc.simple_kmod >= 0) {
            glUniform4fv(g_gx.uloc.simple_kmod, 1, kmod);
        }
        if (g_gx.uloc.simple_use_kmod >= 0) {
            glUniform1i(g_gx.uloc.simple_use_kmod, use_kmod);
        }
    } else {
        upload_uniforms();
    }
    g_gx.dirty = 0;
    g_gx.snap_valid = 0;  /* Clear snapshot after uniform upload */

    if (VIGetRetraceCount() < 6 &&
        g_gx.current_primitive == GX_QUADS && g_gx.num_tex_gens == 0 &&
        g_gx.num_tev_stages == 1 && g_gx.tev_stages[0].tex_map == GX_TEXMAP_NULL &&
        is_redundant_boot_clear_quad(g_gx.vertex_buffer, count)) {
        g_gx.vertex_count = 0;
        g_gx.current_vertex_idx = 0;
        return;
    }

    /* NOTE: draw_boot_simple_quad disabled — using separate VAO with glDrawArrays
     * triggers a different Metal pipeline key that crashes Apple's shader compiler
     * on ARM64 Macs. All draws now go through the main VAO with glDrawElements. */

    /* Draw with appropriate primitive */
    GLenum gl_prim;
    switch (g_gx.current_primitive) {
        case GX_QUADS:         gl_prim = GL_TRIANGLES; break;
        case GX_TRIANGLES:     gl_prim = GL_TRIANGLES; break;
        case GX_TRIANGLESTRIP: gl_prim = GL_TRIANGLE_STRIP; break;
        case GX_TRIANGLEFAN:   gl_prim = GL_TRIANGLE_FAN; break;
        case GX_LINES:         gl_prim = GL_LINES; break;
        case GX_LINESTRIP:     gl_prim = GL_LINE_STRIP; break;
        case GX_POINTS:        gl_prim = GL_POINTS; break;
        default:               gl_prim = GL_TRIANGLES; break;
    }

    const PCGXVertex* vertex_data = g_gx.vertex_buffer;
    int draw_count = count;

    /* Upload vertex data + re-setup attributes.
     * Re-setting vertex attribs each draw prevents Metal pipeline
     * compilation crashes on macOS ARM64. */
    glBindVertexArray(g_gx.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_gx.vbo);
    glBufferData(GL_ARRAY_BUFFER, draw_count * sizeof(PCGXVertex), vertex_data, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PCGXVertex),
                          (void*)offsetof(PCGXVertex, position));
    if (pc_gx_tev_is_simple_shader(shader)) {
        glDisableVertexAttribArray(1);
        glVertexAttrib3f(1, 0.0f, 0.0f, 1.0f);
    } else {
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PCGXVertex),
                              (void*)offsetof(PCGXVertex, normal));
    }
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PCGXVertex),
                          (void*)offsetof(PCGXVertex, color0));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(PCGXVertex),
                          (void*)offsetof(PCGXVertex, texcoord));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gx.ebo);

    /* Viewport: GX viewport is in EFB coords, map to window */
    float sx = (float)g_pc_window_w / (float)PC_GC_WIDTH;
    float sy = (float)g_pc_window_h / (float)PC_GC_HEIGHT;
    glViewport((int)(g_gx.viewport[0] * sx),
               (int)((PC_GC_HEIGHT - g_gx.viewport[1] - g_gx.viewport[3]) * sy),
               (int)(g_gx.viewport[2] * sx),
               (int)(g_gx.viewport[3] * sy));

    /* Scissor */
    if (g_gx.scissor[2] > 0 && g_gx.scissor[3] > 0) {
        int sc_left = g_gx.scissor[0] + (s_scissor_box_off_x / 2);
        int sc_top = g_gx.scissor[1] + (s_scissor_box_off_y / 2);
        int sc_w = g_gx.scissor[2];
        int sc_h = g_gx.scissor[3];
        if (sc_left < 0) sc_left = 0;
        if (sc_top < 0) sc_top = 0;
        if (sc_left > PC_GC_WIDTH) sc_left = PC_GC_WIDTH;
        if (sc_top > PC_GC_HEIGHT) sc_top = PC_GC_HEIGHT;
        if (sc_left + sc_w > PC_GC_WIDTH) sc_w = PC_GC_WIDTH - sc_left;
        if (sc_top + sc_h > PC_GC_HEIGHT) sc_h = PC_GC_HEIGHT - sc_top;
        glEnable(GL_SCISSOR_TEST);
        glScissor((int)(sc_left * sx),
                  (int)((PC_GC_HEIGHT - sc_top - sc_h) * sy),
                  (int)(sc_w * sx),
                  (int)(sc_h * sy));
    } else {
        glDisable(GL_SCISSOR_TEST);
    }

    /* macOS ARM64 Metal driver bug: the first glDrawElements after the GL
     * context is touched by game init code crashes in Metal's pipeline
     * compiler (EXC_ARM_DA_ALIGN in compileFunctionRequestInternal).
     *
     * Skip the first few draw calls entirely to avoid this.  The game
     * only renders black frames during logo fade-in anyway, so the visual
     * impact is negligible. After the skip period, draws proceed normally.
     * Also wrap in crash protection as a safety net. */
    /* Validate shader program before draw to avoid Metal pipeline crashes.
     * On macOS ARM64, Metal compiles pipeline variants lazily. If a new
     * state combo triggers a compiler bug, glDrawElements crashes with
     * SIGBUS. Using glValidateProgram detects this without crashing. */
    {
        GLint validated = GL_TRUE;
#ifdef __APPLE__
        glValidateProgram(shader);
        glGetProgramiv(shader, GL_VALIDATE_STATUS, &validated);
        if (!validated) {
            static int s_validate_fail = 0;
            if (s_validate_fail++ < 5) {
                fprintf(stderr, "[GX] Shader validation failed — skipping draw (prim=%d verts=%d pretrans=%d)\n",
                        g_gx.current_primitive, count, g_gx.batch_pretransformed);
            }
        }
#endif
        /* Log vertex position bounds for diagnostic */
        {
            static int s_vtx_log = 0;
            static int s_zero_draws = 0;
            static int s_offscreen_draws = 0;
            static int s_visible_draws = 0;
            u32 r = VIGetRetraceCount();
            if (r > 500 && r < 510) {
                float minX=1e9, minY=1e9, minZ=1e9;
                float maxX=-1e9, maxY=-1e9, maxZ=-1e9;
                bool all_zero = true;
                for (int vi = 0; vi < count; vi++) {
                    float x = vertex_data[vi].position[0];
                    float y = vertex_data[vi].position[1];
                    float z = vertex_data[vi].position[2];
                    if (x!=0||y!=0||z!=0) all_zero = false;
                    if (x<minX) minX=x; if (x>maxX) maxX=x;
                    if (y<minY) minY=y; if (y>maxY) maxY=y;
                    if (z<minZ) minZ=z; if (z>maxZ) maxZ=z;
                }
                if (all_zero) s_zero_draws++;
                else if (maxX < -1e6 || minX > 1e6 || maxY < -1e6 || minY > 1e6) s_offscreen_draws++;
                else s_visible_draws++;

                if (s_vtx_log < 30 && !all_zero) {
                    fprintf(stderr, "[VTX-POS] verts=%d pos=[%.0f..%.0f, %.0f..%.0f, %.0f..%.0f] pretrans=%d col0=(%.2f,%.2f,%.2f)\n",
                            count, minX, maxX, minY, maxY, minZ, maxZ,
                            g_gx.batch_pretransformed,
                            vertex_data[0].color0[0]/255.0f,
                            vertex_data[0].color0[1]/255.0f,
                            vertex_data[0].color0[2]/255.0f);
                    s_vtx_log++;
                }
            }
            if (r == 510 && s_vtx_log > 0) {
                fprintf(stderr, "[VTX-SUMMARY] zero=%d offscreen=%d visible=%d\n",
                        s_zero_draws, s_offscreen_draws, s_visible_draws);
                s_vtx_log = -1; /* don't repeat */
            }
        }

        if (validated) {
            if (g_gx.current_primitive == GX_QUADS) {
                int num_quads = count / 4;
                int num_indices = num_quads * 6;
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gx.ebo);
                glDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, 0);
            } else {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_linear_ebo);
                glDrawElements(gl_prim, draw_count, GL_UNSIGNED_SHORT, 0);
            }
            s_frame_draws_ok++;
        } else {
            s_frame_draws_crash++;
        }
    }

    g_gx.vertex_count = 0;
    g_gx.current_vertex_idx = 0;
    g_gx.batch_pretransformed = 0;
}

void pc_gx_flush_if_begin_complete(void) {
    if (g_gx.in_begin && g_gx.vertex_count >= g_gx.expected_vertex_count) {
        if (g_gx.vertex_pending) {
            /* commit the last vertex before flushing */
            if (g_gx.current_vertex_idx < PC_GX_MAX_VERTS) {
                g_gx.vertex_buffer[g_gx.current_vertex_idx] = g_gx.current_vertex;
                g_gx.current_vertex_idx++;
            }
            g_gx.vertex_pending = 0;
        }
        pc_gx_flush_vertices();
        g_gx.in_begin = 0;
    }
}

/* ============================================================
 * Vertex submission functions
 * ============================================================ */
extern "C" {

/* Helper: commit current vertex to buffer, carry forward color */
static void commit_vertex(void) {
    if (g_gx.current_vertex_idx < PC_GX_MAX_VERTS) {
        g_gx.vertex_buffer[g_gx.current_vertex_idx] = g_gx.current_vertex;
        g_gx.current_vertex_idx++;
        g_gx.vertex_count++;
    }
    g_gx.vertex_pending = 0;
    /* Carry forward colors to next vertex (GX behavior) */
    unsigned char c0[4], c1[4];
    memcpy(c0, g_gx.current_vertex.color0, 4);
    memcpy(c1, g_gx.current_vertex.color1, 4);
    memset(&g_gx.current_vertex, 0, sizeof(PCGXVertex));
    memcpy(g_gx.current_vertex.color0, c0, 4);
    memcpy(g_gx.current_vertex.color1, c1, 4);
    pc_gx_flush_if_begin_complete();
}

/* GXCmd */
void GXCmd1u8(const u8 x) { (void)x; }
void GXCmd1u16(const u16 x) { (void)x; }
void GXCmd1u32(const u32 x) { (void)x; }

/* GXParam */
void GXParam1u8(const u8 x) { (void)x; }
void GXParam1u16(const u16 x) { (void)x; }
void GXParam1u32(const u32 x) { (void)x; }
void GXParam1s8(const s8 x) { (void)x; }
void GXParam1s16(const s16 x) { (void)x; }
void GXParam1s32(const s32 x) { (void)x; }
void GXParam1f32(const f32 x) { (void)x; }
void GXParam3f32(const f32 x, const f32 y, const f32 z) { (void)x; (void)y; (void)z; }
void GXParam4f32(const f32 x, const f32 y, const f32 z, const f32 w) { (void)x; (void)y; (void)z; (void)w; }

/* GXPosition */
void GXPosition3f32(const f32 x, const f32 y, const f32 z) {
    float pos[3] = {x, y, z};
    if (g_gx.cpu_xform_vertices) {
        float transformed[3];
        int matrix_id = g_gx.active_pnmtx;
        /* Display-list draws often select the matrix via indexed XF load
         * (g_gx.current_mtx) without emitting PNMTXIDX per vertex. */
        if (g_gx.vtx_desc[GX_VA_PNMTXIDX] == GX_NONE) {
            matrix_id = g_gx.current_mtx;
        }
        pc_gx_apply_pos_mtx(matrix_id, pos, transformed);
        pos[0] = transformed[0];
        pos[1] = transformed[1];
        pos[2] = transformed[2];
    }
    if (g_gx.vertex_pending) commit_vertex();
    g_gx.current_vertex.position[0] = pos[0];
    g_gx.current_vertex.position[1] = pos[1];
    g_gx.current_vertex.position[2] = pos[2];
    g_gx.vertex_pending = 1;
}
void GXPosition3u8(const u8 x, const u8 y, const u8 z) { GXPosition3f32(x, y, z); }
void GXPosition3s8(const s8 x, const s8 y, const s8 z) { GXPosition3f32(x, y, z); }
void GXPosition3u16(const u16 x, const u16 y, const u16 z) { GXPosition3f32(x, y, z); }
void GXPosition3s16(const s16 x, const s16 y, const s16 z) { GXPosition3f32(x, y, z); }
void GXPosition2f32(const f32 x, const f32 y) { GXPosition3f32(x, y, 0); }
void GXPosition2u8(const u8 x, const u8 y) { GXPosition3f32(x, y, 0); }
void GXPosition2s8(const s8 x, const s8 y) { GXPosition3f32(x, y, 0); }
void GXPosition2u16(const u16 x, const u16 y) { GXPosition3f32(x, y, 0); }
void GXPosition2s16(const s16 x, const s16 y) { GXPosition3f32(x, y, 0); }

/* Indexed position: look up in array set via GXSetArray */
void GXPosition1x16(const u16 idx) {
    pc_gx_submit_indexed_attr(g_gx.current_vtxfmt, GX_VA_POS, idx);
}
void GXPosition1x8(const u8 idx) { GXPosition1x16(idx); }

/* GXNormal */
void GXNormal3f32(const f32 x, const f32 y, const f32 z) {
    float nrm[3] = {x, y, z};
    if (g_gx.cpu_xform_vertices) {
        float transformed[3];
        int matrix_id = g_gx.active_pnmtx;
        if (g_gx.vtx_desc[GX_VA_PNMTXIDX] == GX_NONE) {
            matrix_id = g_gx.current_mtx;
        }
        pc_gx_apply_nrm_mtx(matrix_id, nrm, transformed);
        nrm[0] = transformed[0];
        nrm[1] = transformed[1];
        nrm[2] = transformed[2];
    }
    g_gx.current_vertex.normal[0] = nrm[0];
    g_gx.current_vertex.normal[1] = nrm[1];
    g_gx.current_vertex.normal[2] = nrm[2];
}
void GXNormal3s16(const s16 x, const s16 y, const s16 z) {
    GXNormal3f32(x / 32767.0f, y / 32767.0f, z / 32767.0f);
}
void GXNormal3s8(const s8 x, const s8 y, const s8 z) {
    GXNormal3f32(x / 127.0f, y / 127.0f, z / 127.0f);
}
void GXNormal1x16(const u16 idx) {
    pc_gx_submit_indexed_attr(g_gx.current_vtxfmt, GX_VA_NRM, idx);
}
void GXNormal1x8(const u8 idx) { GXNormal1x16(idx); }

/* GXColor */
void GXColor4u8(const u8 r, const u8 g, const u8 b, const u8 a) {
    g_gx.current_vertex.color0[0] = r;
    g_gx.current_vertex.color0[1] = g;
    g_gx.current_vertex.color0[2] = b;
    g_gx.current_vertex.color0[3] = a;
}
void GXColor1u32(const u32 x) {
    GXColor4u8(GXCOLOR_R(x), GXCOLOR_G(x), GXCOLOR_B(x), GXCOLOR_A(x));
}
void GXColor3u8(const u8 r, const u8 g, const u8 b) { GXColor4u8(r, g, b, 255); }
void GXColor1u16(const u16 x) {
    /* RGB565 */
    u8 r = (u8)(((x >> 11) & 0x1F) * 255 / 31);
    u8 g = (u8)(((x >> 5) & 0x3F) * 255 / 63);
    u8 b = (u8)((x & 0x1F) * 255 / 31);
    GXColor4u8(r, g, b, 255);
}
void GXColor1x16(const u16 idx) {
    pc_gx_submit_indexed_attr(g_gx.current_vtxfmt, GX_VA_CLR0, idx);
}
void GXColor1x8(const u8 idx) { GXColor1x16(idx); }

/* GXTexCoord */
void GXTexCoord2f32(const f32 x, const f32 y) {
    g_gx.current_vertex.texcoord[0][0] = x;
    g_gx.current_vertex.texcoord[0][1] = y;
}
void GXTexCoord2s16(const s16 x, const s16 y) {
    float scale = (g_gx.tc_frac[0] > 0) ? (1.0f / (1 << g_gx.tc_frac[0])) : 1.0f;
    GXTexCoord2f32(x * scale, y * scale);
}
void GXTexCoord2u16(const u16 x, const u16 y) {
    float scale = (g_gx.tc_frac[0] > 0) ? (1.0f / (1 << g_gx.tc_frac[0])) : 1.0f;
    GXTexCoord2f32(x * scale, y * scale);
}
void GXTexCoord2s8(const s8 x, const s8 y) {
    float scale = (g_gx.tc_frac[0] > 0) ? (1.0f / (1 << g_gx.tc_frac[0])) : 1.0f;
    GXTexCoord2f32(x * scale, y * scale);
}
void GXTexCoord2u8(const u8 x, const u8 y) {
    float scale = (g_gx.tc_frac[0] > 0) ? (1.0f / (1 << g_gx.tc_frac[0])) : 1.0f;
    GXTexCoord2f32(x * scale, y * scale);
}
void GXTexCoord1f32(const f32 x) { GXTexCoord2f32(x, 0); }
void GXTexCoord1s16(const s16 x) {
    float scale = (g_gx.tc_frac[0] > 0) ? (1.0f / (1 << g_gx.tc_frac[0])) : 1.0f;
    GXTexCoord2f32(x * scale, 0);
}
void GXTexCoord1u16(const u16 x) {
    float scale = (g_gx.tc_frac[0] > 0) ? (1.0f / (1 << g_gx.tc_frac[0])) : 1.0f;
    GXTexCoord2f32(x * scale, 0);
}
void GXTexCoord1s8(const s8 x) {
    float scale = (g_gx.tc_frac[0] > 0) ? (1.0f / (1 << g_gx.tc_frac[0])) : 1.0f;
    GXTexCoord2f32(x * scale, 0);
}
void GXTexCoord1u8(const u8 x) {
    float scale = (g_gx.tc_frac[0] > 0) ? (1.0f / (1 << g_gx.tc_frac[0])) : 1.0f;
    GXTexCoord2f32(x * scale, 0);
}
void GXTexCoord1x16(const u16 idx) {
    pc_gx_submit_indexed_attr(g_gx.current_vtxfmt, GX_VA_TEX0, idx);
}
void GXTexCoord1x8(const u8 idx) { GXTexCoord1x16(idx); }

/* GXMatrixIndex */
void GXMatrixIndex1u8(const u8 x) { pc_gx_set_active_matrix_attr(GX_VA_PNMTXIDX, x); }

/* ============================================================
 * GX API functions
 * ============================================================ */

void GXInit(void* base, u32 size) { (void)base; (void)size; }
void GXAbortFrame(void) {}

void GXBegin(u32 primitive, u32 vtxfmt, u16 nVerts) {
    /* Auto-flush previous batch if still in_begin (omitted GXEnd) */
    if (g_gx.in_begin && g_gx.current_vertex_idx > 0) {
        if (g_gx.vertex_pending) commit_vertex();
        pc_gx_flush_vertices();
    }
    g_gx.current_primitive = primitive;
    g_gx.current_vtxfmt = vtxfmt;
    g_gx.expected_vertex_count = nVerts;
    g_gx.vertex_count = 0;
    g_gx.current_vertex_idx = 0;
    g_gx.in_begin = 1;
    g_gx.vertex_pending = 0;
    g_gx.active_pnmtx = g_gx.current_mtx;

    /* Snapshot TEV state at GXBegin time — on real GCN hardware, TEV state
     * is latched when the draw is submitted. Code after GXEnd may modify
     * TEV state for the next draw, but the current draw should use the state
     * that was active at GXBegin time. */
    g_gx.snap_num_tev_stages = g_gx.num_tev_stages;
    memcpy(g_gx.snap_tev_stages, g_gx.tev_stages, sizeof(g_gx.tev_stages));
    g_gx.snap_num_tex_gens = g_gx.num_tex_gens;
    g_gx.snap_num_chans = g_gx.num_chans;
    g_gx.snap_alpha_comp0 = g_gx.alpha_comp0;
    g_gx.snap_alpha_ref0 = g_gx.alpha_ref0;
    g_gx.snap_alpha_op = g_gx.alpha_op;
    g_gx.snap_alpha_comp1 = g_gx.alpha_comp1;
    g_gx.snap_alpha_ref1 = g_gx.alpha_ref1;
    /* Snapshot modelview — J2DPicture::draw cleanup overwrites pos_mtx[0]
     * with identity AFTER GXEnd, but the auto-flush inside GXEnd may not
     * complete before the cleanup runs on some platforms. */
    {
        int idx = g_gx.current_mtx / 3;
        if (idx < 0 || idx >= 10) idx = 0;
        memcpy(g_gx.snap_pos_mtx, g_gx.pos_mtx[idx], sizeof(g_gx.snap_pos_mtx));
        g_gx.snap_current_mtx = g_gx.current_mtx;
    }
    g_gx.snap_valid = 1;
    memset(&g_gx.current_vertex, 0, sizeof(PCGXVertex));
    /* Set default white color */
    g_gx.current_vertex.color0[0] = 255;
    g_gx.current_vertex.color0[1] = 255;
    g_gx.current_vertex.color0[2] = 255;
    g_gx.current_vertex.color0[3] = 255;
}

void GXEnd(void) {
    if (g_gx.vertex_pending) commit_vertex();
    pc_gx_flush_vertices();
    g_gx.in_begin = 0;
}

/* Display list */
void GXBeginDisplayList(void* list, u32 size) { (void)list; (void)size; }
u32 GXEndDisplayList(void) { return 0; }
/* read_be* helpers defined earlier in the file */

static int pc_gx_calc_vertex_size(u32 vtxfmt) {
    int size = 0;
    for (int a = GX_VA_PNMTXIDX; a <= GX_VA_TEX7; a++) {
        u32 desc = g_gx.vtx_desc[a];
        if (desc == GX_NONE) {
            continue;
        }

        if (desc == GX_DIRECT) {
            size += pc_gx_attr_size(vtxfmt, a);
        } else if (desc == GX_INDEX8) {
            size += 1;
        } else if (desc == GX_INDEX16) {
            size += 2;
        }
    }
    return size;
}

static int s_dl_calls_this_frame = 0;
static int s_dl_prims_this_frame = 0;
static u32 s_dl_last_retrace = 0;
void GXCallDisplayList(const void* list, u32 nbytes) {
    {
        u32 r = VIGetRetraceCount();
        if (r != s_dl_last_retrace) {
            static int s_dl_frame_log = 0;
            if (s_dl_frame_log < 10 && s_dl_calls_this_frame > 0 && r > 370) {
                fprintf(stderr, "[DL-STATS] frame %u: %d DL calls, %d primitives\n",
                        s_dl_last_retrace, s_dl_calls_this_frame, s_dl_prims_this_frame);
                s_dl_frame_log++;
            }
            s_dl_calls_this_frame = 0;
            s_dl_prims_this_frame = 0;
            s_dl_last_retrace = r;
        }
        s_dl_calls_this_frame++;
    }
    static int s_gxcdl = 0;
    if (g_pc_verbose && (s_gxcdl++ < 500 || nbytes > 5000)) {
        u8 first = list ? ((const u8*)list)[0] : 0;
        /* Only log non-NOP display lists (skip VcdVatCmd which starts with 0x00) */
        if (first != 0x00 || nbytes > 200) {
            fprintf(stderr, "[GX] GXCallDisplayList: list=%p size=%u first4=%02x%02x%02x%02x\n",
                    list, nbytes,
                    ((const u8*)list)[0], ((const u8*)list)[1],
                    ((const u8*)list)[2], ((const u8*)list)[3]);
            fflush(stderr);
        }
    }
    if (list == NULL || nbytes == 0) {
        return;
    }

    const u8* p = (const u8*)list;
    u8* swapped_words_buf = NULL;
    /* DISABLED: The byte-swap heuristic corrupts display list data more than it
     * helps. The J3D GD buffer writes are in correct big-endian format via
     * J3DGDWrite_u32 (byte-by-byte), and shape DLs from BMD files are also
     * big-endian. The heuristic was swapping data that didn't need swapping,
     * which caused TEV, color, and matrix registers to be misinterpreted. */
    if (false && nbytes >= 8 && (nbytes % 4) == 0 &&
        p[0] == 0x00 && p[2] == 0x00 &&
        ((p[3] & 0x80) != 0 || p[3] == 0x61 || p[3] == 0x10 || p[3] == 0x08))
    {
        swapped_words_buf = (u8*)malloc(nbytes);
        if (swapped_words_buf != NULL) {
            for (u32 i = 0; i < nbytes; i += 4) {
                swapped_words_buf[i + 0] = p[i + 3];
                swapped_words_buf[i + 1] = p[i + 2];
                swapped_words_buf[i + 2] = p[i + 1];
                swapped_words_buf[i + 3] = p[i + 0];
            }
            static int s_unswap_dl_log = 0;
            if (g_pc_verbose && s_unswap_dl_log++ < 24) {
                fprintf(stderr, "[GX] unswapped word-swapped DL: list=%p size=%u\n", list, nbytes);
                fflush(stderr);
            }
            p = swapped_words_buf;
        }
    }
    const u8* end = p + nbytes;
    int primitive_cmd_count = 0;

    /* Validate: first non-NOP byte must be a known GC FIFO command */
    {
        const u8* scan = p;
        while (scan < end && *scan == 0x00) scan++;
        if (scan < end) {
            u8 first = *scan;
            if (first != 0x08 && first != 0x10 &&
                !(first >= 0x20 && first <= 0x38 && (first & 0x07) == 0) &&
                first != 0x61 && !(first >= 0x80 && first <= 0xBF)) {
                return; /* Not a valid DL — skip */
            }
        }
    }

    while (p < end) {
        u8 cmd = *p;

        if (cmd == 0x00) {
            p++;
        } else if (cmd == 0x08) {
            if (p + 6 > end) {
                break;
            }

            u8 addr = p[1];
            u32 val = read_be32(p + 2);
            if (addr == 0x50) {
                g_gx.vtx_desc[GX_VA_PNMTXIDX] = (val >> 0) & 1;
                g_gx.vtx_desc[GX_VA_TEX0MTXIDX] = (val >> 1) & 1;
                g_gx.vtx_desc[GX_VA_TEX1MTXIDX] = (val >> 2) & 1;
                g_gx.vtx_desc[GX_VA_TEX2MTXIDX] = (val >> 3) & 1;
                g_gx.vtx_desc[GX_VA_TEX3MTXIDX] = (val >> 4) & 1;
                g_gx.vtx_desc[GX_VA_TEX4MTXIDX] = (val >> 5) & 1;
                g_gx.vtx_desc[GX_VA_TEX5MTXIDX] = (val >> 6) & 1;
                g_gx.vtx_desc[GX_VA_TEX6MTXIDX] = (val >> 7) & 1;
                g_gx.vtx_desc[GX_VA_TEX7MTXIDX] = (val >> 8) & 1;
                g_gx.vtx_desc[GX_VA_POS] = (val >> 9) & 3;
                g_gx.vtx_desc[GX_VA_NRM] = (val >> 11) & 3;
                g_gx.vtx_desc[GX_VA_CLR0] = (val >> 13) & 3;
                g_gx.vtx_desc[GX_VA_CLR1] = (val >> 15) & 3;
            } else if (addr == 0x60) {
                g_gx.vtx_desc[GX_VA_TEX0] = (val >> 0) & 3;
                g_gx.vtx_desc[GX_VA_TEX1] = (val >> 2) & 3;
                g_gx.vtx_desc[GX_VA_TEX2] = (val >> 4) & 3;
                g_gx.vtx_desc[GX_VA_TEX3] = (val >> 6) & 3;
                g_gx.vtx_desc[GX_VA_TEX4] = (val >> 8) & 3;
                g_gx.vtx_desc[GX_VA_TEX5] = (val >> 10) & 3;
                g_gx.vtx_desc[GX_VA_TEX6] = (val >> 12) & 3;
                g_gx.vtx_desc[GX_VA_TEX7] = (val >> 14) & 3;
            } else if (addr >= 0x70 && addr <= 0x77) {
                int vtxfmt = addr - 0x70;
                if (vtxfmt >= 0 && vtxfmt < PC_GX_MAX_VTXFMT) {
                    auto set_fmt = [&](int attr, int cnt, int type, int frac) {
                        if (attr >= 0 && attr < PC_GX_MAX_ATTR) {
                            g_gx.vtx_fmt[vtxfmt].cnt[attr] = cnt;
                            g_gx.vtx_fmt[vtxfmt].type[attr] = type;
                            g_gx.vtx_fmt[vtxfmt].frac[attr] = frac;
                        }
                    };
                    int posCnt = (val >> 0) & 0x1;
                    int posFmt = (val >> 1) & 0x7;
                    int posShift = (val >> 4) & 0x1F;
                    int nrmCnt = (val >> 9) & 0x1;
                    int nrmFmt = (val >> 10) & 0x7;
                    int col0Cnt = (val >> 13) & 0x1;
                    int col0Fmt = (val >> 14) & 0x7;
                    int col1Cnt = (val >> 17) & 0x1;
                    int col1Fmt = (val >> 18) & 0x7;
                    int tex0Cnt = (val >> 21) & 0x1;
                    int tex0Fmt = (val >> 22) & 0x7;
                    int tex0Shift = (val >> 25) & 0x1F;

                    int nrmShift = 0;
                    if (nrmFmt == GX_S8) {
                        nrmShift = 6;
                    } else if (nrmFmt == GX_S16) {
                        nrmShift = 14;
                    }

                    set_fmt(GX_VA_POS, posCnt, posFmt, posShift);
                    set_fmt(GX_VA_NRM, nrmCnt, nrmFmt, nrmShift);
                    set_fmt(GX_VA_CLR0, col0Cnt, col0Fmt, 0);
                    set_fmt(GX_VA_CLR1, col1Cnt, col1Fmt, 0);
                    set_fmt(GX_VA_TEX0, tex0Cnt, tex0Fmt, tex0Shift);
                }
            } else if (addr >= 0x80 && addr <= 0x87) {
                int vtxfmt = addr - 0x80;
                if (vtxfmt >= 0 && vtxfmt < PC_GX_MAX_VTXFMT) {
                    auto set_fmt = [&](int attr, int cnt, int type, int frac) {
                        if (attr >= 0 && attr < PC_GX_MAX_ATTR) {
                            g_gx.vtx_fmt[vtxfmt].cnt[attr] = cnt;
                            g_gx.vtx_fmt[vtxfmt].type[attr] = type;
                            g_gx.vtx_fmt[vtxfmt].frac[attr] = frac;
                        }
                    };
                    set_fmt(GX_VA_TEX1, (val >> 0) & 0x1, (val >> 1) & 0x7, (val >> 4) & 0x1F);
                    set_fmt(GX_VA_TEX2, (val >> 9) & 0x1, (val >> 10) & 0x7, (val >> 13) & 0x1F);
                    set_fmt(GX_VA_TEX3, (val >> 18) & 0x1, (val >> 19) & 0x7, (val >> 22) & 0x1F);
                    set_fmt(GX_VA_TEX4, (val >> 27) & 0x1, (val >> 28) & 0x7,
                            g_gx.vtx_fmt[vtxfmt].frac[GX_VA_TEX4]);
                }
            } else if (addr >= 0x90 && addr <= 0x97) {
                int vtxfmt = addr - 0x90;
                if (vtxfmt >= 0 && vtxfmt < PC_GX_MAX_VTXFMT) {
                    g_gx.vtx_fmt[vtxfmt].frac[GX_VA_TEX4] = (val >> 0) & 0x1F;
                    g_gx.vtx_fmt[vtxfmt].cnt[GX_VA_TEX5] = (val >> 5) & 0x1;
                    g_gx.vtx_fmt[vtxfmt].type[GX_VA_TEX5] = (val >> 6) & 0x7;
                    g_gx.vtx_fmt[vtxfmt].frac[GX_VA_TEX5] = (val >> 9) & 0x1F;
                    g_gx.vtx_fmt[vtxfmt].cnt[GX_VA_TEX6] = (val >> 14) & 0x1;
                    g_gx.vtx_fmt[vtxfmt].type[GX_VA_TEX6] = (val >> 15) & 0x7;
                    g_gx.vtx_fmt[vtxfmt].frac[GX_VA_TEX6] = (val >> 18) & 0x1F;
                    g_gx.vtx_fmt[vtxfmt].cnt[GX_VA_TEX7] = (val >> 23) & 0x1;
                    g_gx.vtx_fmt[vtxfmt].type[GX_VA_TEX7] = (val >> 24) & 0x7;
                    g_gx.vtx_fmt[vtxfmt].frac[GX_VA_TEX7] = (val >> 27) & 0x1F;
                }
            } else if (addr >= 0xB0 && addr < 0xC0) {
                int attr = addr - 0xB0;
                if (attr < PC_GX_MAX_ATTR) {
                    g_gx.array_stride[attr] = val & 0xFF;
                }
            }
            p += 6;
        } else if (cmd == 0x10) {
            if (p + 5 > end) {
                break;
            }

            u16 count_minus1 = read_be16(p + 1);
            u16 xf_addr = read_be16(p + 3);
            u32 data_count = count_minus1 + 1;
            u32 data_bytes = data_count * 4;
            if (p + 5 + data_bytes > end) {
                break;
            }

            if (xf_addr < 0x0078 && data_count == 12) {
                int slot = xf_addr / 12;
                if (slot >= 0 && slot < 10) {
                    const u8* d = p + 5;
                    for (int i = 0; i < 12; i++) {
                        ((float*)g_gx.pos_mtx[slot])[i] = read_be_f32(d + i * 4);
                    }
                    DIRTY(PC_GX_DIRTY_MODELVIEW);
                }
            } else if (xf_addr >= 0x0078 && xf_addr < 0x00F0 && data_count == 9) {
                int slot = (xf_addr - 0x0078) / 9;
                if (slot >= 0 && slot < 10) {
                    const u8* d = p + 5;
                    for (int i = 0; i < 9; i++) {
                        ((float*)g_gx.nrm_mtx[slot])[i] = read_be_f32(d + i * 4);
                    }
                    DIRTY(PC_GX_DIRTY_MODELVIEW);
                }
            } else if (xf_addr >= 0x0104 && xf_addr < 0x017C && data_count == 12) {
                int slot = (xf_addr - 0x0104) / 12;
                if (slot >= 0 && slot < 10) {
                    const u8* d = p + 5;
                    for (int i = 0; i < 12; i++) {
                        ((float*)g_gx.tex_mtx[slot])[i] = read_be_f32(d + i * 4);
                    }
                    DIRTY(PC_GX_DIRTY_TEXGEN);
                }
            } else {
                /* Parse ALL XF register writes by iterating over each register in the batch.
                 * Handles single writes (data_count=1) and multi-register writes like:
                 *   - loadMatColors: XF 0x100C, 2 regs (mat color 0-1)
                 *   - loadAmbColors: XF 0x100A, 2 regs (amb color 0-1)
                 *   - channel control: XF 0x100E, 4 regs (color0/1, alpha0/1 ctrl)
                 */
                const u8* d = p + 5;
                for (u32 ri = 0; ri < data_count && ri < 16; ri++) {
                    u32 reg_addr = xf_addr + ri;
                    u32 val = read_be32(d + ri * 4);
                    switch (reg_addr) {
                    case 0x1009: /* XF_NUMCHANS */
                        g_gx.num_chans = val & 0x3;
                        DIRTY(PC_GX_DIRTY_LIGHTING);
                        break;
                    case 0x100A:   /* XF_AMBCOLOR0 */
                    case 0x100B: { /* XF_AMBCOLOR1 */
                        int ci = reg_addr - 0x100A; /* 0 or 1 */
                        g_gx.chan_amb_color[ci][0] = ((val >> 24) & 0xFF) / 255.0f; /* R */
                        g_gx.chan_amb_color[ci][1] = ((val >> 16) & 0xFF) / 255.0f; /* G */
                        g_gx.chan_amb_color[ci][2] = ((val >>  8) & 0xFF) / 255.0f; /* B */
                        g_gx.chan_amb_color[ci][3] = ((val >>  0) & 0xFF) / 255.0f; /* A */
                        DIRTY(PC_GX_DIRTY_LIGHTING);
                        break;
                    }
                    case 0x100C:   /* XF_MATCOLOR0 */
                    case 0x100D: { /* XF_MATCOLOR1 */
                        int ci = reg_addr - 0x100C; /* 0 or 1 */
                        g_gx.chan_mat_color[ci][0] = ((val >> 24) & 0xFF) / 255.0f; /* R */
                        g_gx.chan_mat_color[ci][1] = ((val >> 16) & 0xFF) / 255.0f; /* G */
                        g_gx.chan_mat_color[ci][2] = ((val >>  8) & 0xFF) / 255.0f; /* B */
                        g_gx.chan_mat_color[ci][3] = ((val >>  0) & 0xFF) / 255.0f; /* A */
                        DIRTY(PC_GX_DIRTY_LIGHTING);
                        break;
                    }
                    case 0x100E:   /* XF_COLOR0CNTRL */
                    case 0x100F:   /* XF_COLOR1CNTRL */
                    case 0x1010:   /* XF_ALPHA0CNTRL */
                    case 0x1011: { /* XF_ALPHA1CNTRL */
                        int idx = reg_addr - 0x100E; /* 0..3 */
                        if (idx >= 0 && idx < 4) {
                            /* XF channel control register layout (GCN hardware):
                             * bit 0: MatSrc (0=REG, 1=VTX)
                             * bit 1: Enable (lighting)
                             * bits 2-5: LightMask[3:0]
                             * bit 6: AmbSrc (0=REG, 1=VTX)
                             * bits 7-8: DiffuseFn
                             * bit 9: AttnEnable
                             * bit 10: AttnFn
                             * bits 11-14: LightMask[7:4] */
                            g_gx.chan_ctrl_mat_src[idx] = val & 1;          /* bit 0 = MatSrc */
                            g_gx.chan_ctrl_enable[idx] = (val >> 1) & 1;    /* bit 1 = Enable */
                            g_gx.chan_ctrl_amb_src[idx] = (val >> 6) & 1;   /* bit 6 = AmbSrc */
                            g_gx.chan_ctrl_light_mask[idx] = ((val >> 2) & 0xF) | (((val >> 11) & 0xF) << 4);
                            DIRTY(PC_GX_DIRTY_LIGHTING);
                        }
                        break;
                    }
                    case 0x103F: /* XF_NUMTEXGENS */
                        g_gx.num_tex_gens = val & 0xF;
                        DIRTY(PC_GX_DIRTY_TEXGEN);
                        break;
                    default:
                        /* XF light registers: 8 lights × 16 regs each at 0x0600-0x067F
                         * Light structure: [0-2]=unused, [3]=color, [4-6]=cos_atten,
                         * [7-9]=dist_atten, [10-12]=position, [13-15]=direction */
                        if (reg_addr >= 0x0600 && reg_addr < 0x0680) {
                            int light_idx = (reg_addr - 0x0600) / 16;
                            int light_reg = (reg_addr - 0x0600) % 16;
                            if (light_idx >= 0 && light_idx < 8) {
                                if (light_reg == 3) { /* color */
                                    g_gx.lights[light_idx].color[0] = ((val >> 24) & 0xFF) / 255.0f;
                                    g_gx.lights[light_idx].color[1] = ((val >> 16) & 0xFF) / 255.0f;
                                    g_gx.lights[light_idx].color[2] = ((val >>  8) & 0xFF) / 255.0f;
                                    g_gx.lights[light_idx].color[3] = ((val >>  0) & 0xFF) / 255.0f;
                                    DIRTY(PC_GX_DIRTY_LIGHTING);
                                } else if (light_reg >= 10 && light_reg <= 12) { /* position x/y/z */
                                    union { u32 u; float f; } conv;
                                    conv.u = val;
                                    g_gx.lights[light_idx].pos[light_reg - 10] = conv.f;
                                    DIRTY(PC_GX_DIRTY_LIGHTING);
                                }
                            }
                        }
                        break;
                    }
                }
            }
            p += 5 + data_bytes;
        } else if (cmd >= 0x20 && cmd <= 0x3F) {
            if (p + 5 > end) {
                break;
            }
            /* Indexed XF load: on GCN this reads matrix data from an external array
             * and stores it into XF memory. On PC, the matrix was already loaded by
             * J3DFifoLoadPosMtxImm (called from J3DShapeMtx or the base matrix path).
             * DON'T change current_mtx here — it was already set by GXSetCurrentMtx
             * to point to the slot where the matrix was loaded. Changing it would
             * point to an empty slot and produce zero vertex positions. */
            /* (cmd 0x20-0x3F consumed but not processed) */
            p += 5;
        } else if (cmd == 0x61) {
            if (p + 5 > end) {
                break;
            }
            u32 bp = read_be32(p + 1);
            u8 reg = (u8)(bp >> 24);
            u32 data = bp & 0x00FFFFFF;
            if (reg == 0x00) {
                /* BP_GEN_MODE */
                int old_tev = g_gx.num_tev_stages;
                g_gx.num_tex_gens = data & 0x0F;
                g_gx.num_tev_stages = ((data >> 10) & 0x0F) + 1;
                {
                    static int s_genmode_log = 0;
                    u32 r = VIGetRetraceCount();
                    if (r > 500 && s_genmode_log < 60 && old_tev != g_gx.num_tev_stages) {
                        fprintf(stderr, "[BP-GENMODE] tev %d→%d texgens=%d list=%p size=%u first=%02x\n",
                                old_tev, g_gx.num_tev_stages, g_gx.num_tex_gens,
                                list, nbytes, ((const u8*)list)[0]);
                        s_genmode_log++;
                    }
                }
                int cm_hw = (data >> 14) & 0x03;
                switch (cm_hw) {
                case 0: g_gx.cull_mode = GX_CULL_NONE; break;
                case 1: g_gx.cull_mode = GX_CULL_BACK; break;
                case 2: g_gx.cull_mode = GX_CULL_FRONT; break;
                default: g_gx.cull_mode = GX_CULL_ALL; break;
                }
                DIRTY(PC_GX_DIRTY_TEXGEN | PC_GX_DIRTY_TEV_STAGES | PC_GX_DIRTY_CULL);
            } else if (reg >= 0x28 && reg <= 0x2F) {
                /* BP_TEV_ORDER: two stages per BP register */
                int pair = reg - 0x28;
                int stage0 = pair * 2;
                int stage1 = stage0 + 1;

                int map0 = (data >> 0) & 0x07;
                int coord0 = (data >> 3) & 0x07;
                int enable0 = (data >> 6) & 0x01;
                int color0 = (data >> 7) & 0x07;
                if (g_pc_verbose) {
                    static int s_tevord_log = 0;
                    if (s_tevord_log++ < 20) {
                        fprintf(stderr, "[BP] TEV_ORDER reg=0x%02x data=0x%06x s0=%d map0=%d coord0=%d en0=%d\n",
                                reg, data, stage0, map0, coord0, enable0);
                    }
                }

                int map1 = (data >> 12) & 0x07;
                int coord1 = (data >> 15) & 0x07;
                int enable1 = (data >> 18) & 0x01;
                int color1 = (data >> 19) & 0x07;

                if (stage0 < PC_GX_MAX_TEV_STAGES) {
                    g_gx.tev_stages[stage0].tex_map = enable0 ? map0 : GX_TEXMAP_NULL;
                    g_gx.tev_stages[stage0].tex_coord = enable0 ? coord0 : GX_TEXCOORD_NULL;
                    g_gx.tev_stages[stage0].color_chan = color0;
                }
                if (stage1 < PC_GX_MAX_TEV_STAGES) {
                    g_gx.tev_stages[stage1].tex_map = enable1 ? map1 : GX_TEXMAP_NULL;
                    g_gx.tev_stages[stage1].tex_coord = enable1 ? coord1 : GX_TEXCOORD_NULL;
                    g_gx.tev_stages[stage1].color_chan = color1;
                }
                DIRTY(PC_GX_DIRTY_TEV_STAGES | PC_GX_DIRTY_TEXTURES);
            } else if (reg >= 0xC0 && reg <= 0xDF) {
                /* TEV color/alpha combiner state */
                int stage = (reg - 0xC0) / 2;
                bool is_alpha = ((reg & 1) != 0);
                {
                    static int s_tev_comb_log = 0;
                    u32 r = VIGetRetraceCount();
                    if (r > 500 && s_tev_comb_log < 20) {
                        fprintf(stderr, "[BP-TEV-COMB] reg=0x%02x stage=%d alpha=%d data=0x%06x d=%d c=%d b=%d a=%d\n",
                                reg, stage, is_alpha, data,
                                is_alpha ? ((data>>4)&7) : ((data>>0)&0xF),
                                is_alpha ? ((data>>7)&7) : ((data>>4)&0xF),
                                is_alpha ? ((data>>10)&7) : ((data>>8)&0xF),
                                is_alpha ? ((data>>13)&7) : ((data>>12)&0xF));
                        s_tev_comb_log++;
                    }
                }
                if (stage >= 0 && stage < PC_GX_MAX_TEV_STAGES) {
                    if (!is_alpha) {
                        g_gx.tev_stages[stage].color_d = (data >> 0) & 0xF;
                        g_gx.tev_stages[stage].color_c = (data >> 4) & 0xF;
                        g_gx.tev_stages[stage].color_b = (data >> 8) & 0xF;
                        g_gx.tev_stages[stage].color_a = (data >> 12) & 0xF;
                        g_gx.tev_stages[stage].color_bias = (data >> 16) & 0x3;
                        g_gx.tev_stages[stage].color_op = (data >> 18) & 0x1;
                        g_gx.tev_stages[stage].color_clamp = (data >> 19) & 0x1;
                        g_gx.tev_stages[stage].color_scale = (data >> 20) & 0x3;
                        g_gx.tev_stages[stage].color_out = (data >> 22) & 0x3;
                    } else {
                        g_gx.tev_stages[stage].ras_swap = (data >> 0) & 0x3;
                        g_gx.tev_stages[stage].tex_swap = (data >> 2) & 0x3;
                        g_gx.tev_stages[stage].alpha_d = (data >> 4) & 0x7;
                        g_gx.tev_stages[stage].alpha_c = (data >> 7) & 0x7;
                        g_gx.tev_stages[stage].alpha_b = (data >> 10) & 0x7;
                        g_gx.tev_stages[stage].alpha_a = (data >> 13) & 0x7;
                        g_gx.tev_stages[stage].alpha_bias = (data >> 16) & 0x3;
                        g_gx.tev_stages[stage].alpha_op = (data >> 18) & 0x1;
                        g_gx.tev_stages[stage].alpha_clamp = (data >> 19) & 0x1;
                        g_gx.tev_stages[stage].alpha_scale = (data >> 20) & 0x3;
                        g_gx.tev_stages[stage].alpha_out = (data >> 22) & 0x3;
                    }
                }
                DIRTY(PC_GX_DIRTY_TEV_STAGES | PC_GX_DIRTY_KONST | PC_GX_DIRTY_SWAP_TABLES);
            } else if (reg >= 0xE0 && reg <= 0xE7) {
                /* TEV/K color register payloads */
                int idx = (reg - 0xE0) / 2;
                bool is_ra = ((reg & 1) == 0);
                bool is_k = ((data >> 23) & 1) != 0;
                if (idx >= 0 && idx < 4) {
                    float(*dst)[4] = is_k ? g_gx.tev_k_colors : g_gx.tev_colors;
                    auto norm11 = [](u32 v) -> float {
                        /* Keep approximate normalized 0..1 mapping; enough for logo/constants path. */
                        return ((v > 255u) ? 255.0f : (float)v) / 255.0f;
                    };
                    u32 c0 = (data >> 0) & 0x7FF;
                    u32 c1 = (data >> 12) & 0x7FF;
                    if (is_ra) {
                        dst[idx][0] = norm11(c0);
                        dst[idx][3] = norm11(c1);
                    } else {
                        dst[idx][2] = norm11(c0);
                        dst[idx][1] = norm11(c1);
                    }
                }
                DIRTY(PC_GX_DIRTY_TEV_COLORS | PC_GX_DIRTY_KONST);
            } else if (reg >= 0xF6 && reg <= 0xFD) {
                /* TEV KSel + swap mode table */
                int pair = reg - 0xF6;
                int stage0 = pair * 2;
                int stage1 = stage0 + 1;
                int rb = (data >> 0) & 0x3;
                int ga = (data >> 2) & 0x3;
                int kc0 = (data >> 4) & 0x1F;
                int ka0 = (data >> 9) & 0x1F;
                int kc1 = (data >> 14) & 0x1F;
                int ka1 = (data >> 19) & 0x1F;
                if (stage0 >= 0 && stage0 < PC_GX_MAX_TEV_STAGES) {
                    g_gx.tev_stages[stage0].k_color_sel = kc0;
                    g_gx.tev_stages[stage0].k_alpha_sel = ka0;
                }
                if (stage1 >= 0 && stage1 < PC_GX_MAX_TEV_STAGES) {
                    g_gx.tev_stages[stage1].k_color_sel = kc1;
                    g_gx.tev_stages[stage1].k_alpha_sel = ka1;
                }
                int table = pair / 2;
                bool bg_half = (pair & 1) != 0;
                if (table >= 0 && table < 4) {
                    if (!bg_half) {
                        g_gx.tev_swap_table[table].r = rb;
                        g_gx.tev_swap_table[table].g = ga;
                    } else {
                        g_gx.tev_swap_table[table].b = rb;
                        g_gx.tev_swap_table[table].a = ga;
                    }
                }
                DIRTY(PC_GX_DIRTY_KONST | PC_GX_DIRTY_SWAP_TABLES);
            }
            p += 5;
        } else if ((cmd & 0x80) != 0) {
            u8 prim_type = cmd & 0xF8;
            if (prim_type != 0x80 && prim_type != 0x90 && prim_type != 0x98 &&
                prim_type != 0xA0 && prim_type != 0xA8 && prim_type != 0xB0 &&
                prim_type != 0xB8) {
                p++;
                continue;
            }
            if (p + 3 > end) {
                break;
            }

            u8 vat_idx = cmd & 0x07;
            u16 vtx_count = read_be16(p + 1);
            p += 3;
            primitive_cmd_count++;
            s_dl_prims_this_frame++;

            int vtx_size = pc_gx_calc_vertex_size(vat_idx);
            u32 data_size = (u32)vtx_count * vtx_size;
            if (vtx_size == 0 || p + data_size > end || vtx_count > 10000) {
                if (g_pc_verbose) {
                    static int s_bad_prim_decode = 0;
                    if (s_bad_prim_decode++ < 64) {
                        fprintf(stderr,
                                "[GX] skip primitive cmd=%02x prim=%02x vat=%u vtxCount=%u vtxSize=%d remaining=%ld\n",
                                cmd, prim_type, vat_idx, vtx_count, vtx_size, (long)(end - p));
                        fflush(stderr);
                    }
                }
                /* Not a real draw — back up and skip this byte */
                p -= 2;
                continue;
            }

            u32 gx_prim;
            switch (prim_type) {
            case 0x80: gx_prim = GX_QUADS; break;
            case 0x90: gx_prim = GX_TRIANGLES; break;
            case 0x98: gx_prim = GX_TRIANGLESTRIP; break;
            case 0xA0: gx_prim = GX_TRIANGLEFAN; break;
            case 0xA8: gx_prim = GX_LINES; break;
            case 0xB0: gx_prim = GX_LINESTRIP; break;
            case 0xB8: gx_prim = GX_POINTS; break;
            default: gx_prim = GX_TRIANGLES; break;
            }

            g_gx.cpu_xform_vertices = 1;
            g_gx.batch_pretransformed = 1;
            GXBegin(gx_prim, vat_idx, vtx_count);

            const u8* vp = p;
            static int s_idx_decode_log = 0;
            bool log_indices = g_pc_verbose && VIGetRetraceCount() > 850 && s_idx_decode_log < 24;
            for (int v = 0; v < vtx_count; v++) {
                for (int a = GX_VA_PNMTXIDX; a <= GX_VA_TEX7; a++) {
                    u32 desc = g_gx.vtx_desc[a];
                    if (desc == GX_NONE) {
                        continue;
                    }

                    if (desc == GX_INDEX8 || desc == GX_INDEX16) {
                        u16 idx = (desc == GX_INDEX8) ? *vp++ : read_be16(vp);
                        if (desc == GX_INDEX16) {
                            vp += 2;
                        }
                        if (log_indices && (a == GX_VA_POS || a == GX_VA_NRM || a == GX_VA_TEX0)) {
                            fprintf(stderr, "[GX] idx v=%d attr=%d idx=%u\n", v, a, (unsigned)idx);
                        }
                        pc_gx_submit_indexed_attr(vat_idx, a, idx);
                    } else if (desc == GX_DIRECT) {
                        int attr_size = pc_gx_attr_size(vat_idx, a);
                        if (attr_size <= 0 || vp + attr_size > end) {
                            static bool s_warned_bad_direct = false;
                            if (!s_warned_bad_direct) {
                                s_warned_bad_direct = true;
                                fprintf(stderr, "[GX] unsupported direct attr decode attr=%d fmt=%d size=%d\n",
                                        a, vat_idx, attr_size);
                            }
                            vp = end;
                            break;
                        }

                        pc_gx_submit_attr_data(vp, vat_idx, a);
                        vp += attr_size;
                    }
                }
                /* Commit vertex to buffer after all attributes decoded */
                commit_vertex();
                if (g_pc_verbose) {
                    static int s_dl_commit_log = 0;
                    if (s_dl_commit_log < 10) {
                        fprintf(stderr, "[GX-DL] committed vertex %d/%d vtxIdx=%d vtxCount=%d expected=%d\n",
                                v, vtx_count, g_gx.current_vertex_idx, g_gx.vertex_count,
                                g_gx.expected_vertex_count);
                        s_dl_commit_log++;
                    }
                }
            }
            if (log_indices) {
                fflush(stderr);
                s_idx_decode_log++;
            }

            GXEnd();
            g_gx.cpu_xform_vertices = 0;
            if (vp != p + data_size) {
                static bool s_warned_dl_alignment = false;
                if (!s_warned_dl_alignment) {
                    s_warned_dl_alignment = true;
                    fprintf(stderr, "[GX] DL vertex decode misaligned: fmt=%u expected=%u got=%ld\n",
                            vat_idx, data_size, (long)(vp - p));
                }
            }
            p = vp;
        } else {
            p++;
        }
    }

    /* Log large DLs (geometry data) that produce no primitives */
    if (primitive_cmd_count == 0 && nbytes > 64) {
        static int s_no_prim_log = 0;
        if (s_no_prim_log++ < 10) {
            const u8* raw = (const u8*)list;
            fprintf(stderr,
                    "[DL-EMPTY] size=%u prims=0 first16=%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
                    nbytes, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
                    raw[8], raw[9], raw[10], raw[11], raw[12], raw[13], raw[14], raw[15]);
        }
    }
    if (swapped_words_buf != NULL) {
        free(swapped_words_buf);
    }
}

/* Vertex format */
void GXSetVtxDesc(u32 attr, u32 type) { if (attr < PC_GX_MAX_ATTR) g_gx.vtx_desc[attr] = type; }
void GXClearVtxDesc(void) { memset(g_gx.vtx_desc, 0, sizeof(g_gx.vtx_desc)); }
void GXSetVtxAttrFmt(u32 vtxfmt, u32 attr, u32 cnt, u32 type, u32 frac) {
    if (vtxfmt >= PC_GX_MAX_VTXFMT || attr >= PC_GX_MAX_ATTR) {
        return;
    }

    PCGXVertexFormat* fmt = &g_gx.vtx_fmt[vtxfmt];
    fmt->cnt[attr] = cnt;
    fmt->type[attr] = type;
    fmt->frac[attr] = frac;
    fmt->valid[attr] = 1;

    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
        g_gx.tc_frac[attr - GX_VA_TEX0] = frac;
    } else if (attr == GX_VA_POS) {
        g_gx.pos_frac = frac;
    } else if (attr == GX_VA_NRM || attr == GX_VA_NBT) {
        g_gx.nrm_frac = frac;
    }
}
void GXSetArray(u32 attr, const void* base, u8 stride) {
    if (attr < PC_GX_MAX_ATTR) {
        g_gx.array_base[attr] = base;
        g_gx.array_stride[attr] = stride;
        if (base == NULL) {
            s_array_count_limit[attr] = 0;
        } else if (s_array_count_limit[attr] == 0) {
            s_array_count_limit[attr] = 0xFFFFFFFFu;
        }
    }
}
void GXInvalidateVtxCache(void) {}
void GXSetTexCoordGen(u32 dst_coord, u32 func, u32 src_param, u32 mtx) {
    if (dst_coord < 8) {
        g_gx.tex_gen_type[dst_coord] = func;
        g_gx.tex_gen_src[dst_coord] = src_param;
        g_gx.tex_gen_mtx[dst_coord] = mtx;
        DIRTY(PC_GX_DIRTY_TEXGEN);
    }
}
void GXSetTexCoordGen2(u32 dst_coord, u32 func, u32 src_param, u32 mtx, u32 normalize, u32 postmtx) {
    (void)normalize; (void)postmtx;
    GXSetTexCoordGen(dst_coord, func, src_param, mtx);
}
void GXSetNumTexGens(u32 nTexGens) { g_gx.num_tex_gens = nTexGens; DIRTY(PC_GX_DIRTY_TEXGEN); }

/* Transform */
void GXLoadPosMtxImm(const void* mtx, u32 id) {
    if (id / 3 < 10) memcpy(g_gx.pos_mtx[id / 3], mtx, 48);
    DIRTY(PC_GX_DIRTY_MODELVIEW);
}
void GXLoadNrmMtxImm(const void* mtx, u32 id) {
    if (id / 3 < 10) memcpy(g_gx.nrm_mtx[id / 3], mtx, 36);
    DIRTY(PC_GX_DIRTY_MODELVIEW);
}
void GXLoadNrmMtxImm3x3(const void* mtx, u32 id) {
    if (id / 3 < 10) memcpy(g_gx.nrm_mtx[id / 3], mtx, 36);
    DIRTY(PC_GX_DIRTY_MODELVIEW);
}
void GXLoadTexMtxImm(const void* mtx, u32 id, u32 type) {
    (void)type;
    if (id / 3 < 10) memcpy(g_gx.tex_mtx[id / 3], mtx, 48);
    DIRTY(PC_GX_DIRTY_TEXGEN);
}
void GXSetCurrentMtx(u32 id) {
    g_gx.current_mtx = id;
    g_gx.active_pnmtx = id;
    DIRTY(PC_GX_DIRTY_MODELVIEW);
}
void GXLoadProjectionMtx(const void* mtx, u32 type) {
    memcpy(g_gx.projection_mtx, mtx, 64);
    g_gx.projection_type = type;
    DIRTY(PC_GX_DIRTY_PROJECTION);
}

/* Viewport & scissor */
void GXSetViewport(f32 x, f32 y, f32 w, f32 h, f32 nearz, f32 farz) {
    /* GX viewport inputs sometimes use negative origins (e.g. y=-100) that are
     * valid for GX's internal transform but map poorly to GL viewport space.
     * Clamp to EFB bounds to keep boot/logo 2D composition stable. */
    float vx = x;
    float vy = y;
    float vw = w;
    float vh = h;
    if (vx < 0.0f) vx = 0.0f;
    if (vy < 0.0f) vy = 0.0f;
    if (vw <= 0.0f || vw > (float)PC_GC_WIDTH) vw = (float)PC_GC_WIDTH;
    if (vh <= 0.0f || vh > (float)PC_GC_HEIGHT) vh = (float)PC_GC_HEIGHT;
    if (vx + vw > (float)PC_GC_WIDTH) vx = (float)PC_GC_WIDTH - vw;
    if (vy + vh > (float)PC_GC_HEIGHT) vy = (float)PC_GC_HEIGHT - vh;

    g_gx.viewport[0] = vx; g_gx.viewport[1] = vy;
    g_gx.viewport[2] = vw; g_gx.viewport[3] = vh;
    g_gx.viewport[4] = nearz; g_gx.viewport[5] = farz;
    if (g_pc_verbose) {
        static int s_vp_log = 0;
        if (s_vp_log++ < 80) {
            fprintf(stderr, "[GX] GXSetViewport: in=(%.2f,%.2f,%.2f,%.2f) out=(%.2f,%.2f,%.2f,%.2f) n=%.3f f=%.3f frame=%u\n",
                    x, y, w, h, vx, vy, vw, vh, nearz, farz, VIGetRetraceCount());
        }
    }
}
void GXSetViewportJitter(f32 x, f32 y, f32 w, f32 h, f32 nearz, f32 farz, u32 field) {
    GXSetViewport(x, y, w, h, nearz, farz);
    (void)field;
}
void GXSetScissor(u32 left, u32 top, u32 w, u32 h) {
    g_gx.scissor[0] = left; g_gx.scissor[1] = top;
    g_gx.scissor[2] = w; g_gx.scissor[3] = h;
    if (g_pc_verbose) {
        static int s_sc_log = 0;
        if (s_sc_log++ < 120) {
            fprintf(stderr, "[GX] GXSetScissor: l=%u t=%u w=%u h=%u off=(%d,%d) frame=%u\n",
                    left, top, w, h, s_scissor_box_off_x, s_scissor_box_off_y, VIGetRetraceCount());
        }
    }
}

void GXSetScissorBoxOffset(s32 x_off, s32 y_off) {
    s_scissor_box_off_x = x_off;
    s_scissor_box_off_y = y_off;
    if (g_pc_verbose) {
        static int s_scoff_log = 0;
        if (s_scoff_log++ < 80) {
            fprintf(stderr, "[GX] GXSetScissorBoxOffset: x=%d y=%d frame=%u\n",
                    x_off, y_off, VIGetRetraceCount());
        }
    }
}

/* TEV */
void GXSetNumTevStages(u32 nStages) {
    g_gx.num_tev_stages = nStages; DIRTY(PC_GX_DIRTY_TEV_STAGES);
}
void GXSetTevOrder(u32 stage, u32 texCoord, u32 texMap, u32 colorChan) {
    if (stage < PC_GX_MAX_TEV_STAGES) {
        g_gx.tev_stages[stage].tex_coord = texCoord;
        g_gx.tev_stages[stage].tex_map = texMap;
        g_gx.tev_stages[stage].color_chan = colorChan;
        DIRTY(PC_GX_DIRTY_TEV_STAGES);
    }
}
void GXSetTevColorIn(u32 stage, u32 a, u32 b, u32 c, u32 d) {
    if (stage < PC_GX_MAX_TEV_STAGES) {
        g_gx.tev_stages[stage].color_a = a; g_gx.tev_stages[stage].color_b = b;
        g_gx.tev_stages[stage].color_c = c; g_gx.tev_stages[stage].color_d = d;
        DIRTY(PC_GX_DIRTY_TEV_STAGES);
    }
}
void GXSetTevAlphaIn(u32 stage, u32 a, u32 b, u32 c, u32 d) {
    if (stage < PC_GX_MAX_TEV_STAGES) {
        g_gx.tev_stages[stage].alpha_a = a; g_gx.tev_stages[stage].alpha_b = b;
        g_gx.tev_stages[stage].alpha_c = c; g_gx.tev_stages[stage].alpha_d = d;
        DIRTY(PC_GX_DIRTY_TEV_STAGES);
    }
}
void GXSetTevColorOp(u32 stage, u32 op, u32 bias, u32 scale, u32 clamp, u32 out) {
    if (stage < PC_GX_MAX_TEV_STAGES) {
        g_gx.tev_stages[stage].color_op = op; g_gx.tev_stages[stage].color_bias = bias;
        g_gx.tev_stages[stage].color_scale = scale; g_gx.tev_stages[stage].color_clamp = clamp;
        g_gx.tev_stages[stage].color_out = out;
        DIRTY(PC_GX_DIRTY_TEV_STAGES);
    }
}
void GXSetTevAlphaOp(u32 stage, u32 op, u32 bias, u32 scale, u32 clamp, u32 out) {
    if (stage < PC_GX_MAX_TEV_STAGES) {
        g_gx.tev_stages[stage].alpha_op = op; g_gx.tev_stages[stage].alpha_bias = bias;
        g_gx.tev_stages[stage].alpha_scale = scale; g_gx.tev_stages[stage].alpha_clamp = clamp;
        g_gx.tev_stages[stage].alpha_out = out;
        DIRTY(PC_GX_DIRTY_TEV_STAGES);
    }
}
void GXSetTevColor(u32 reg, u32 color) {
    if (reg < 4) {
        g_gx.tev_colors[reg][0] = GXCOLOR_R(color) / 255.0f;
        g_gx.tev_colors[reg][1] = GXCOLOR_G(color) / 255.0f;
        g_gx.tev_colors[reg][2] = GXCOLOR_B(color) / 255.0f;
        g_gx.tev_colors[reg][3] = GXCOLOR_A(color) / 255.0f;
        DIRTY(PC_GX_DIRTY_TEV_COLORS);
    }
}
void GXSetTevColorS10(u32 reg, u32 color) { GXSetTevColor(reg, color); }
void GXSetTevKColor(u32 reg, u32 color) {
    if (reg < 4) {
        g_gx.tev_k_colors[reg][0] = GXCOLOR_R(color) / 255.0f;
        g_gx.tev_k_colors[reg][1] = GXCOLOR_G(color) / 255.0f;
        g_gx.tev_k_colors[reg][2] = GXCOLOR_B(color) / 255.0f;
        g_gx.tev_k_colors[reg][3] = GXCOLOR_A(color) / 255.0f;
        DIRTY(PC_GX_DIRTY_KONST);
    }
}
void GXSetTevKColorSel(u32 stage, u32 sel) {
    if (stage < PC_GX_MAX_TEV_STAGES) { g_gx.tev_stages[stage].k_color_sel = sel; DIRTY(PC_GX_DIRTY_KONST); }
}
void GXSetTevKAlphaSel(u32 stage, u32 sel) {
    if (stage < PC_GX_MAX_TEV_STAGES) { g_gx.tev_stages[stage].k_alpha_sel = sel; DIRTY(PC_GX_DIRTY_KONST); }
}
void GXSetTevSwapMode(u32 stage, u32 ras_sel, u32 tex_sel) {
    if (stage < PC_GX_MAX_TEV_STAGES) {
        g_gx.tev_stages[stage].ras_swap = ras_sel;
        g_gx.tev_stages[stage].tex_swap = tex_sel;
        DIRTY(PC_GX_DIRTY_SWAP_TABLES);
    }
}
void GXSetTevSwapModeTable(u32 table, u32 r, u32 g, u32 b, u32 a) {
    if (table < 4) {
        g_gx.tev_swap_table[table].r = r; g_gx.tev_swap_table[table].g = g;
        g_gx.tev_swap_table[table].b = b; g_gx.tev_swap_table[table].a = a;
        DIRTY(PC_GX_DIRTY_SWAP_TABLES);
    }
}
void GXSetTevOp(u32 stage, u32 mode) {
    if (mode == 0 /* GX_MODULATE */) {
        GXSetTevColorIn(stage, 8, 4, 0, 15);
        GXSetTevAlphaIn(stage, 7, 4, 1, 7);
        GXSetTevColorOp(stage, 0, 0, 0, 1, 0);
        GXSetTevAlphaOp(stage, 0, 0, 0, 1, 0);
    } else if (mode == 1 /* GX_DECAL */) {
        GXSetTevColorIn(stage, 0, 8, 9, 15);
        GXSetTevAlphaIn(stage, 7, 7, 7, 5);
        GXSetTevColorOp(stage, 0, 0, 0, 1, 0);
        GXSetTevAlphaOp(stage, 0, 0, 0, 1, 0);
    } else if (mode == 2 /* GX_BLEND */) {
        GXSetTevColorIn(stage, 8, 0, 10, 15);
        GXSetTevAlphaIn(stage, 7, 4, 1, 7);
        GXSetTevColorOp(stage, 0, 0, 0, 1, 0);
        GXSetTevAlphaOp(stage, 0, 0, 0, 1, 0);
    } else if (mode == 3 /* GX_REPLACE */) {
        GXSetTevColorIn(stage, 15, 15, 15, 8);
        GXSetTevAlphaIn(stage, 7, 7, 7, 4);
        GXSetTevColorOp(stage, 0, 0, 0, 1, 0);
        GXSetTevAlphaOp(stage, 0, 0, 0, 1, 0);
    } else if (mode == 4 /* GX_PASSCLR */) {
        GXSetTevColorIn(stage, 15, 15, 15, 10);
        GXSetTevAlphaIn(stage, 7, 7, 7, 5);
        GXSetTevColorOp(stage, 0, 0, 0, 1, 0);
        GXSetTevAlphaOp(stage, 0, 0, 0, 1, 0);
    }
}
void GXSetTevDirect(u32 stage) {
    if (stage < PC_GX_MAX_TEV_STAGES) {
        g_gx.tev_stages[stage].ind_stage = 0;
        g_gx.tev_stages[stage].ind_format = 0;
        g_gx.tev_stages[stage].ind_bias = 0;
        g_gx.tev_stages[stage].ind_mtx = 0;
        g_gx.tev_stages[stage].ind_wrap_s = 0;
        g_gx.tev_stages[stage].ind_wrap_t = 0;
        g_gx.tev_stages[stage].ind_add_prev = 0;
    }
}
void GXSetTevIndirect(u32 stage, u32 indStage, u32 format, u32 biasSel,
                       u32 mtxSel, u32 wrapS, u32 wrapT, u32 addPrev, u32 utcLod, u32 alphaSel) {
    if (stage < PC_GX_MAX_TEV_STAGES) {
        g_gx.tev_stages[stage].ind_stage = indStage;
        g_gx.tev_stages[stage].ind_format = format;
        g_gx.tev_stages[stage].ind_bias = biasSel;
        g_gx.tev_stages[stage].ind_mtx = mtxSel;
        g_gx.tev_stages[stage].ind_wrap_s = wrapS;
        g_gx.tev_stages[stage].ind_wrap_t = wrapT;
        g_gx.tev_stages[stage].ind_add_prev = addPrev;
        g_gx.tev_stages[stage].ind_lod = utcLod;
        g_gx.tev_stages[stage].ind_alpha = alphaSel;
        DIRTY(PC_GX_DIRTY_INDIRECT);
    }
}

/* Indirect textures */
void GXSetIndTexOrder(u32 indStage, u32 texCoord, u32 texMap) {
    if (indStage < 4) {
        g_gx.ind_order[indStage].tex_coord = texCoord;
        g_gx.ind_order[indStage].tex_map = texMap;
    }
}
void GXSetNumIndStages(u32 nStages) { g_gx.num_ind_stages = nStages; DIRTY(PC_GX_DIRTY_INDIRECT); }
void GXSetIndTexMtx(u32 mtxSel, const void* offset, s8 scaleExp) {
    (void)mtxSel; (void)offset; (void)scaleExp;
}
void GXSetIndTexCoordScale(u32 indStage, u32 scaleS, u32 scaleT) {
    if (indStage < 4) {
        g_gx.ind_order[indStage].scale_s = scaleS;
        g_gx.ind_order[indStage].scale_t = scaleT;
    }
}

/* Textures */
void GXInitTexObj(void* obj, void* data, u16 w, u16 h, u32 fmt, u32 wrapS, u32 wrapT, u32 mipmap) {
    PCTexObj* tex = (PCTexObj*)obj;
    memset(tex, 0, sizeof(PCTexObj));
    tex->image_ptr = data;
    tex->width = w;
    tex->height = h;
    tex->format = fmt;
    tex->wrap_s = wrapS;
    tex->wrap_t = wrapT;
    tex->mipmap = mipmap;
    tex->min_filter = 1; /* GX_LINEAR */
    tex->mag_filter = 1;
    tex->tlut_name = 0xFFFFFFFF; /* no TLUT */
    tex->gl_tex = 0;
}
void GXInitTexObjCI(void* obj, void* data, u16 w, u16 h, u32 fmt, u32 wrapS, u32 wrapT, u32 mipmap, u32 tlut) {
    GXInitTexObj(obj, data, w, h, fmt, wrapS, wrapT, mipmap);
    PCTexObj* tex = (PCTexObj*)obj;
    tex->ci_format = fmt;
    tex->tlut_name = tlut;
}
void GXInitTexObjLOD(void* obj, u32 minFilt, u32 magFilt, f32 minLOD, f32 maxLOD,
                      f32 lodBias, u32 biasClamp, u32 doEdgeLOD, u32 maxAniso) {
    PCTexObj* tex = (PCTexObj*)obj;
    tex->min_filter = minFilt;
    tex->mag_filter = magFilt;
    tex->min_lod = minLOD;
    tex->max_lod = maxLOD;
    tex->lod_bias = lodBias;
    (void)biasClamp; (void)doEdgeLOD;
    tex->max_aniso = maxAniso;
}

/* No-flush variant for reinitTexture — just clears the GL texture slot */
extern "C" void pc_gx_load_tex_obj_nf(void* obj, u32 mapID) {
    if (mapID >= 8) return;
    g_gx.gl_textures[mapID] = 0;
    DIRTY(PC_GX_DIRTY_TEXTURES);
}

void GXLoadTexObj(void* obj, u32 mapID) {
    pc_platform_ensure_gl_context_current();
    PCTexObj* tex = (PCTexObj*)obj;
    if (mapID == GX_TEXMAP_NULL) {
        /* Some 2D logo paths pass TEXMAP_NULL even though a texture is expected. */
        mapID = GX_TEXMAP0;
    }
    if (!tex || !tex->image_ptr || mapID >= 8) {
        return;
    }
    if (g_gx.current_vertex_idx > 0) {
        /* Prevent texture-state bleed between back-to-back quads when callers
         * update TEXMAP state while vertex data is still queued for flush. */
        if (g_gx.vertex_pending) {
            commit_vertex();
        }
        pc_gx_flush_vertices();
    }
    /* Check texture cache first */
    const void* tlut_ptr = NULL;
    int tlut_fmt = 0, tlut_cnt = 0;
    u32 tlut_hash = 0;
    u32 data_hash = 0;
    if (tex->tlut_name < 16 && g_gx.tlut[tex->tlut_name].data) {
        tlut_ptr = g_gx.tlut[tex->tlut_name].data;
        tlut_fmt = g_gx.tlut[tex->tlut_name].format;
        tlut_cnt = g_gx.tlut[tex->tlut_name].n_entries;
        tlut_hash = pc_gx_tlut_hash(tlut_ptr, tlut_fmt, tlut_cnt);
    }
    data_hash = pc_gx_texture_data_hash(tex->image_ptr, tex->width, tex->height, tex->format);

    GLuint gl_tex = pc_gx_texture_cache_lookup(tex->image_ptr, tex->width, tex->height,
                                               tex->format, tex->tlut_name, tlut_ptr,
                                               tlut_hash, data_hash,
                                               tex->wrap_s, tex->wrap_t, tex->min_filter);
    if (gl_tex == 0) {
        /* Decode and upload */
        gl_tex = pc_gx_texture_decode_and_upload(tex->image_ptr, tex->width, tex->height,
                                                 tex->format, (void*)tlut_ptr, tlut_fmt, tlut_cnt);
        if (gl_tex) {
            glBindTexture(GL_TEXTURE_2D, gl_tex);
            GLenum gl_filter = tex->min_filter ? GL_LINEAR : GL_NEAREST;
            GLenum gl_wrap_s = (tex->wrap_s == GX_MIRROR) ? GL_MIRRORED_REPEAT :
                               (tex->wrap_s == GX_CLAMP) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
            GLenum gl_wrap_t = (tex->wrap_t == GX_MIRROR) ? GL_MIRRORED_REPEAT :
                               (tex->wrap_t == GX_CLAMP) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap_s);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap_t);
            pc_gx_texture_cache_insert(tex->image_ptr, tex->width, tex->height,
                                       tex->format, tex->tlut_name, tlut_ptr,
                                       tlut_hash, data_hash,
                                       tex->wrap_s, tex->wrap_t, tex->min_filter,
                                       gl_tex);
            /* Flush after new texture creation — on macOS ARM64, creating GL
             * resources mid-render-pass corrupts Metal's pipeline state.
             * glFlush finalizes the resource before the next draw/clear. */
            glFlush();
        }
    }

    if (gl_tex) {
        g_gx.gl_textures[mapID] = gl_tex;
        g_gx.tex_obj_w[mapID] = tex->width;
        g_gx.tex_obj_h[mapID] = tex->height;
        g_gx.tex_obj_fmt[mapID] = tex->format;
        s_last_loaded_tex_map = (int)mapID;
        DIRTY(PC_GX_DIRTY_TEXTURES);
    }
}

void GXInitTlutObj(void* obj, void* data, u32 fmt, u16 nEntries) {
    /* Store TLUT data pointer in the obj (we just use it as a pass-through) */
    (void)obj; (void)data; (void)fmt; (void)nEntries;
}
void GXLoadTlut(void* obj, u32 tlutName) {
    /* The game's TLUT loading path: store pointer for later CI texture decode */
    (void)obj;
    if (tlutName < 16) {
        /* The TLUT data will be referenced when GXLoadTexObj is called with a CI texture */
        /* The actual tlut data pointer is passed via GXInitTlutObj; for now we handle this
         * through the GXInitTexObjCI path which sets tlut_name */
    }
}
void GXInvalidateTexAll(void) {
    /* GC invalidates TMEM bindings here. On PC the cache is keyed by source content,
     * so deleting live GL textures on every invalidate causes driver instability. */
}
u32 GXGetTexBufferSize(u16 w, u16 h, u32 fmt, u32 mipmap, u32 maxLOD) {
    (void)mipmap; (void)maxLOD;
    /* Rough size estimate per format */
    u32 pixels = w * h;
    switch (fmt) {
        case 0x0: /* I4 */  return pixels / 2;
        case 0x1: /* I8 */  return pixels;
        case 0x2: /* IA4 */ return pixels;
        case 0x3: /* IA8 */ return pixels * 2;
        case 0x4: /* RGB565 */ return pixels * 2;
        case 0x5: /* RGB5A3 */ return pixels * 2;
        case 0x6: /* RGBA8 */ return pixels * 4;
        case 0x8: /* C4 */  return pixels / 2;
        case 0x9: /* C8 */  return pixels;
        case 0xA: /* C14X2 */ return pixels * 2;
        case 0xE: /* CMPR */ return pixels / 2;
        default: return pixels * 4;
    }
}
void GXSetTexCopySrc(u32 left, u32 top, u32 w, u32 h) {
    g_gx.tex_copy_src[0] = left; g_gx.tex_copy_src[1] = top;
    g_gx.tex_copy_src[2] = w; g_gx.tex_copy_src[3] = h;
}
void GXSetTexCopyDst(u16 w, u16 h, u32 fmt, u32 mipmap) {
    g_gx.tex_copy_dst[0] = w; g_gx.tex_copy_dst[1] = h;
    g_gx.tex_copy_fmt = fmt; g_gx.tex_copy_mipmap = mipmap;
}
void GXCopyTex(void* dest, u32 clear) { (void)dest; (void)clear; }

/* Channels / Lighting */
void GXSetNumChans(u32 nChans) { g_gx.num_chans = nChans; DIRTY(PC_GX_DIRTY_LIGHTING); }
void GXSetChanMatColor(u32 chan, u32 color) {
    /* GX channel IDs: 0=COLOR0, 1=COLOR1, 2=ALPHA0, 3=ALPHA1,
     * 4=COLOR0A0 (sets both 0 and 1), 5=COLOR1A1 (sets both 2 and 3) */
    float r = GXCOLOR_R(color) / 255.0f;
    float g = GXCOLOR_G(color) / 255.0f;
    float b = GXCOLOR_B(color) / 255.0f;
    float a = GXCOLOR_A(color) / 255.0f;
    if (chan == 4) { /* GX_COLOR0A0 */
        g_gx.chan_mat_color[0][0] = r; g_gx.chan_mat_color[0][1] = g;
        g_gx.chan_mat_color[0][2] = b; g_gx.chan_mat_color[0][3] = a;
        DIRTY(PC_GX_DIRTY_LIGHTING);
    } else if (chan == 5) { /* GX_COLOR1A1 */
        g_gx.chan_mat_color[1][0] = r; g_gx.chan_mat_color[1][1] = g;
        g_gx.chan_mat_color[1][2] = b; g_gx.chan_mat_color[1][3] = a;
        DIRTY(PC_GX_DIRTY_LIGHTING);
    } else if (chan < 2) {
        g_gx.chan_mat_color[chan][0] = r; g_gx.chan_mat_color[chan][1] = g;
        g_gx.chan_mat_color[chan][2] = b; g_gx.chan_mat_color[chan][3] = a;
        DIRTY(PC_GX_DIRTY_LIGHTING);
    }
}
void GXSetChanAmbColor(u32 chan, u32 color) {
    float r = GXCOLOR_R(color) / 255.0f;
    float g = GXCOLOR_G(color) / 255.0f;
    float b = GXCOLOR_B(color) / 255.0f;
    float a = GXCOLOR_A(color) / 255.0f;
    if (chan == 4) { /* GX_COLOR0A0 */
        g_gx.chan_amb_color[0][0] = r; g_gx.chan_amb_color[0][1] = g;
        g_gx.chan_amb_color[0][2] = b; g_gx.chan_amb_color[0][3] = a;
        DIRTY(PC_GX_DIRTY_LIGHTING);
    } else if (chan == 5) { /* GX_COLOR1A1 */
        g_gx.chan_amb_color[1][0] = r; g_gx.chan_amb_color[1][1] = g;
        g_gx.chan_amb_color[1][2] = b; g_gx.chan_amb_color[1][3] = a;
        DIRTY(PC_GX_DIRTY_LIGHTING);
    } else if (chan < 2) {
        g_gx.chan_amb_color[chan][0] = r; g_gx.chan_amb_color[chan][1] = g;
        g_gx.chan_amb_color[chan][2] = b; g_gx.chan_amb_color[chan][3] = a;
        DIRTY(PC_GX_DIRTY_LIGHTING);
    }
}
void GXSetChanCtrl(u32 chan, u32 enable, u32 ambSrc, u32 matSrc, u32 lightMask, u32 diffFn, u32 attnFn) {
    /* GX channel IDs: COLOR0=0, COLOR1=1, ALPHA0=2, ALPHA1=3, COLOR0A0=4, COLOR1A1=5
     * COLOR0A0 sets BOTH color0 (index 0) and alpha0 (index 1) at once.
     * COLOR1A1 sets BOTH color1 (index 2) and alpha1 (index 3). */
    int indices[2] = {-1, -1};
    int count = 0;
    switch (chan) {
        case 0: indices[0] = 0; count = 1; break; /* GX_COLOR0 */
        case 1: indices[0] = 2; count = 1; break; /* GX_COLOR1 */
        case 2: indices[0] = 1; count = 1; break; /* GX_ALPHA0 */
        case 3: indices[0] = 3; count = 1; break; /* GX_ALPHA1 */
        case 4: indices[0] = 0; indices[1] = 1; count = 2; break; /* GX_COLOR0A0 */
        case 5: indices[0] = 2; indices[1] = 3; count = 2; break; /* GX_COLOR1A1 */
    }
    for (int i = 0; i < count; i++) {
        int idx = indices[i];
        if (idx >= 0 && idx < 4) {
            g_gx.chan_ctrl_enable[idx] = enable;
            g_gx.chan_ctrl_amb_src[idx] = ambSrc;
            g_gx.chan_ctrl_mat_src[idx] = matSrc;
            g_gx.chan_ctrl_light_mask[idx] = lightMask;
            g_gx.chan_ctrl_diff_fn[idx] = diffFn;
            g_gx.chan_ctrl_attn_fn[idx] = attnFn;
        }
    }
    DIRTY(PC_GX_DIRTY_LIGHTING);
}
void GXInitLightPos(void* light, f32 x, f32 y, f32 z) {
    /* GXLightObj is 0x4C bytes; position at offset 0x10 */
    f32* obj = (f32*)light;
    if (obj) { obj[4] = x; obj[5] = y; obj[6] = z; }
}
void GXInitLightDir(void* light, f32 x, f32 y, f32 z) {
    f32* obj = (f32*)light;
    if (obj) { obj[7] = x; obj[8] = y; obj[9] = z; }
}
void GXInitLightColor(void* light, u32 color) {
    u32* obj = (u32*)light;
    if (obj) obj[3] = color;
}
void GXInitLightAttn(void* light, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {
    f32* obj = (f32*)light;
    if (obj) { obj[10] = a0; obj[11] = a1; obj[12] = a2; obj[13] = k0; obj[14] = k1; obj[15] = k2; }
}
void GXInitLightAttnA(void* light, f32 a0, f32 a1, f32 a2) {
    f32* obj = (f32*)light;
    if (obj) { obj[10] = a0; obj[11] = a1; obj[12] = a2; }
}
void GXInitLightAttnK(void* light, f32 k0, f32 k1, f32 k2) {
    f32* obj = (f32*)light;
    if (obj) { obj[13] = k0; obj[14] = k1; obj[15] = k2; }
}
void GXInitLightSpot(void* light, f32 cutoff, u32 spotFn) { (void)light; (void)cutoff; (void)spotFn; }
void GXInitLightDistAttn(void* light, f32 ref_dist, f32 ref_br, u32 distFn) {
    (void)light; (void)ref_dist; (void)ref_br; (void)distFn;
}
void GXLoadLightObjImm(void* light, u32 id) {
    if (!light || id == 0) return;
    /* GXLightID is a bitmask: GX_LIGHT0=1, GX_LIGHT1=2, GX_LIGHT2=4, etc.
     * Convert to array index 0-7 by finding the set bit position. */
    int idx = 0;
    u32 mask = id;
    while (mask > 1 && idx < 7) { mask >>= 1; idx++; }
    f32* obj = (f32*)light;
    u32* uobj = (u32*)light;
    g_gx.lights[idx].pos[0] = obj[4];
    g_gx.lights[idx].pos[1] = obj[5];
    g_gx.lights[idx].pos[2] = obj[6];
    u32 c = uobj[3];
    g_gx.lights[idx].color[0] = GXCOLOR_R(c) / 255.0f;
    g_gx.lights[idx].color[1] = GXCOLOR_G(c) / 255.0f;
    g_gx.lights[idx].color[2] = GXCOLOR_B(c) / 255.0f;
    g_gx.lights[idx].color[3] = GXCOLOR_A(c) / 255.0f;
    DIRTY(PC_GX_DIRTY_LIGHTING);
}
void GXLoadLightObjIndx(u32 ltObj, u32 id) { (void)ltObj; (void)id; }
void GXInitSpecularDir(void* light, f32 nx, f32 ny, f32 nz) { (void)light; (void)nx; (void)ny; (void)nz; }
void GXInitSpecularDirHA(void* light, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz) {
    (void)light; (void)nx; (void)ny; (void)nz; (void)hx; (void)hy; (void)hz;
}

/* Blend & depth */
void GXSetBlendMode(u32 type, u32 srcFactor, u32 dstFactor, u32 logicOp) {
    g_gx.blend_mode = type; g_gx.blend_src = srcFactor;
    g_gx.blend_dst = dstFactor; g_gx.blend_logic_op = logicOp;
    DIRTY(PC_GX_DIRTY_BLEND);
}
void GXSetZMode(u32 compareEnable, u32 func, u32 updateEnable) {
    g_gx.z_compare_enable = compareEnable;
    g_gx.z_compare_func = func;
    g_gx.z_update_enable = updateEnable;
    DIRTY(PC_GX_DIRTY_DEPTH);
}
void GXSetZCompLoc(u32 before_tex) { (void)before_tex; }
void GXSetColorUpdate(u32 enable) { g_gx.color_update_enable = enable; DIRTY(PC_GX_DIRTY_COLOR_MASK); }
void GXSetAlphaUpdate(u32 enable) { g_gx.alpha_update_enable = enable; DIRTY(PC_GX_DIRTY_COLOR_MASK); }
void GXSetDstAlpha(u32 enable, u8 alpha) { (void)enable; (void)alpha; }

/* Alpha compare */
void GXSetAlphaCompare(u32 comp0, u32 ref0, u32 op, u32 comp1, u32 ref1) {
    g_gx.alpha_comp0 = comp0; g_gx.alpha_ref0 = ref0;
    g_gx.alpha_op = op; g_gx.alpha_comp1 = comp1; g_gx.alpha_ref1 = ref1;
    DIRTY(PC_GX_DIRTY_ALPHA_CMP);
}

/* Cull mode */
void GXSetCullMode(u32 mode) { g_gx.cull_mode = mode; DIRTY(PC_GX_DIRTY_CULL); }

/* Fog */
void GXSetFog(u32 type, f32 startZ, f32 endZ, f32 nearZ, f32 farZ, u32 color) {
    g_gx.fog_type = type;
    g_gx.fog_start = startZ; g_gx.fog_end = endZ;
    g_gx.fog_near = nearZ; g_gx.fog_far = farZ;
    g_gx.fog_color[0] = GXCOLOR_R(color) / 255.0f;
    g_gx.fog_color[1] = GXCOLOR_G(color) / 255.0f;
    g_gx.fog_color[2] = GXCOLOR_B(color) / 255.0f;
    g_gx.fog_color[3] = GXCOLOR_A(color) / 255.0f;
    DIRTY(PC_GX_DIRTY_FOG);
}
void GXSetFogRangeAdj(u32 enable, u16 center, void* table) { (void)enable; (void)center; (void)table; }
void GXInitFogAdjTable(void* table, u16 w, const void* projMtx) { (void)table; (void)w; (void)projMtx; }

/* Framebuffer / copy */
void GXSetCopyClear(u32 color, u32 z) {
#if 0
    /* Debug clear color — disabled, use actual game-supplied color */
    g_gx.clear_color[0] = 0.75f;
    g_gx.clear_color[1] = 0.45f;
    g_gx.clear_color[2] = 0.20f;
    g_gx.clear_color[3] = 1.0f;
#else
    g_gx.clear_color[0] = GXCOLOR_R(color) / 255.0f;
    g_gx.clear_color[1] = GXCOLOR_G(color) / 255.0f;
    g_gx.clear_color[2] = GXCOLOR_B(color) / 255.0f;
    g_gx.clear_color[3] = GXCOLOR_A(color) / 255.0f;
#endif
    g_gx.clear_depth = z / 16777215.0f;
    glClearColor(g_gx.clear_color[0], g_gx.clear_color[1], g_gx.clear_color[2], g_gx.clear_color[3]);
    glClearDepth(g_gx.clear_depth);
}
void GXSetCopyFilter(u32 aa, const u8* sample_pattern, u32 vf, const u8* vfilter) {
    (void)aa; (void)sample_pattern; (void)vf; (void)vfilter;
}
void GXSetDispCopySrc(u32 left, u32 top, u32 w, u32 h) {
    g_gx.copy_src[0] = left; g_gx.copy_src[1] = top;
    g_gx.copy_src[2] = w; g_gx.copy_src[3] = h;
}
void GXSetDispCopyDst(u16 w, u16 h) { g_gx.copy_dst[0] = w; g_gx.copy_dst[1] = h; }
static u32 pc_gx_get_num_xfb_lines(u32 efbHt, u32 iScale) {
    if (efbHt == 0) {
        return 1;
    }
    u32 count = (efbHt - 1u) * 0x100u;
    u32 realHt = (iScale != 0) ? ((count / iScale) + 1u) : efbHt;
    u32 iScaleD = iScale;
    if (iScaleD > 0x80u && iScaleD < 0x100u) {
        while ((iScaleD % 2u) == 0u) {
            iScaleD /= 2u;
        }
        if (iScaleD != 0u && (efbHt % iScaleD) == 0u) {
            realHt++;
        }
    }
    if (realHt > 0x400u) {
        realHt = 0x400u;
    }
    return realHt;
}

u32 GXSetDispCopyYScale(f32 yscale) {
    if (yscale < 1.0f) {
        yscale = 1.0f;
    }
    u32 iScale = ((u32)(256.0f / yscale)) & 0x1FFu;
    if (iScale == 0u) {
        iScale = 1u;
    }
    u32 efbHt = (g_gx.copy_src[3] > 0) ? (u32)g_gx.copy_src[3] : (u32)PC_GC_HEIGHT;
    u32 xfbHt = pc_gx_get_num_xfb_lines(efbHt, iScale);
    if (g_pc_verbose) {
        static int s_yscale_log = 0;
        if (s_yscale_log++ < 40) {
            fprintf(stderr, "[GX] GXSetDispCopyYScale: y=%.5f iScale=%u efbH=%u -> xfbH=%u frame=%u\n",
                    yscale, iScale, efbHt, xfbHt, VIGetRetraceCount());
        }
    }
    return xfbHt;
}
u32 GXSetDispCopyFrame2Field(u32 mode) { (void)mode; return 0; }
void GXSetCopyClamp(u32 clamp) { (void)clamp; }
static int s_copy_disp_count = 0;
void GXCopyDisp(void* dest, u32 clear) {
    /* Skip ensure_gl — context is always current in single-threaded mode */
    (void)dest;
    if (g_pc_verbose && s_copy_disp_count < 10) {
        fprintf(stderr, "[GX] GXCopyDisp clear=%d color=(%.2f,%.2f,%.2f,%.2f)\n",
                clear, g_gx.clear_color[0], g_gx.clear_color[1], g_gx.clear_color[2], g_gx.clear_color[3]);
        s_copy_disp_count++;
    }
    if (clear) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glDisable(GL_SCISSOR_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}
void GXSetPixelFmt(u32 pix_fmt, u32 z_fmt) { (void)pix_fmt; (void)z_fmt; }

/* Misc */
void GXSetLineWidth(u8 width, u32 texOffsets) {
    pc_platform_ensure_gl_context_current();
    (void)texOffsets;
    if (width > 0) glLineWidth(width / 6.0f);
}
void GXSetPointSize(u8 size, u32 texOffsets) {
    pc_platform_ensure_gl_context_current();
    (void)texOffsets;
    if (size > 0) glPointSize(size / 6.0f);
}
void GXSetFieldMask(u32 odd, u32 even) { (void)odd; (void)even; }
void GXSetFieldMode(u32 field_mode, u32 half_aspect_ratio) { (void)field_mode; (void)half_aspect_ratio; }
void GXSetDither(u32 enable) { (void)enable; }
void GXFlush(void) {
#ifndef __APPLE__
    glFlush();
#endif
}
void GXPixModeSync(void) {}
void GXTexModeSync(void) {}
void GXPokeAlphaMode(u32 comp, u32 threshold) { (void)comp; (void)threshold; }
void GXPokeAlphaRead(u32 mode) { (void)mode; }
void GXPokeAlphaUpdate(u32 update) { (void)update; }
void GXPokeBlendMode(u32 type, u32 srcFactor, u32 dstFactor, u32 logicOp) {
    (void)type; (void)srcFactor; (void)dstFactor; (void)logicOp;
}
void GXPokeColorUpdate(u32 update) { (void)update; }
void GXPokeDstAlpha(u32 enable, u8 alpha) { (void)enable; (void)alpha; }
void GXPokeDither(u32 enable) { (void)enable; }
void GXPokeZMode(u32 compareEnable, u32 func, u32 updateEnable) {
    (void)compareEnable; (void)func; (void)updateEnable;
}
void GXSetGPMetric(u32 perf0, u32 perf1) { (void)perf0; (void)perf1; }
void GXReadGPMetric(u32* cnt0, u32* cnt1) { if (cnt0) *cnt0 = 0; if (cnt1) *cnt1 = 0; }
void GXClearGPMetric(void) {}
void GXSetVerifyLevel(u32 level) { (void)level; }

/* Draw done */
void GXSetDrawDone(void) {}
void GXWaitDrawDone(void) {}
void GXDrawDone(void) {
    pc_platform_ensure_gl_context_current();
}
void GXSetDrawSync(u16 token) { (void)token; }
u16 GXReadDrawSync(void) { return 0; }
void GXSetMisc(u32 token, u32 val) { (void)token; (void)val; }

/* Perf */
void GXReadXfRasMetric(u32* xfWaitIn, u32* xfWaitOut, u32* rasbusy, u32* clocks) {
    if (xfWaitIn) *xfWaitIn = 0; if (xfWaitOut) *xfWaitOut = 0;
    if (rasbusy) *rasbusy = 0; if (clocks) *clocks = 0;
}

/* Get functions */
void GXGetProjectionv(f32* p) {
    if (p) memcpy(p, g_gx.projection_mtx, 64);
}
void GXGetViewportv(f32* vp) {
    if (vp) memcpy(vp, g_gx.viewport, 24);
}
void GXGetScissor(u32* left, u32* top, u32* w, u32* h) {
    if (left) *left = g_gx.scissor[0]; if (top) *top = g_gx.scissor[1];
    if (w) *w = g_gx.scissor[2]; if (h) *h = g_gx.scissor[3];
}

/* --- Additional GX functions needed by TP --- */
void* GXGetCPUFifo(void) { return NULL; }
void* GXGetGPFifo(void) { return NULL; }
void GXSetGPFifo(void* fifo) { (void)fifo; }
void GXSetCPUFifo(void* fifo) { (void)fifo; }
void GXSaveCPUFifo(void* fifo) { (void)fifo; }
void GXInitFifoBase(void* fifo, void* base, u32 size) { (void)fifo; (void)base; (void)size; }
void GXInitFifoPtrs(void* fifo, void* readPtr, void* writePtr) { (void)fifo; (void)readPtr; (void)writePtr; }
void* GXGetFifoBase(void* fifo) { (void)fifo; return NULL; }
u32 GXGetFifoSize(void* fifo) { (void)fifo; return 0; }
void GXGetGPStatus(u8* overhi, u8* underlo, u8* readIdle, u8* cmdIdle, u8* brkpt) {
    if (overhi) *overhi = 0; if (underlo) *underlo = 0;
    if (readIdle) *readIdle = 1; if (cmdIdle) *cmdIdle = 1; if (brkpt) *brkpt = 0;
}
void* GXGetCurrentGXThread(void) { return NULL; }
void* GXSetCurrentGXThread(void) { return NULL; }
void GXEnableTexOffsets(u32 coord, u32 lineOfs, u32 pointOfs) { (void)coord; (void)lineOfs; (void)pointOfs; }
void GXSetTexCoordScaleManually(u32 coord, u32 enable, u16 ss, u16 ts) { (void)coord; (void)enable; (void)ss; (void)ts; }
void GXSetClipMode(u32 mode) { (void)mode; }
void GXSetCoPlanar(u32 enable) { (void)enable; }
void GXSetDispCopyGamma(u32 gamma) { (void)gamma; }
void GXSetDrawDoneCallback(void* cb) { (void)cb; }
void GXSetZTexture(u32 op, u32 fmt, u32 bias) { (void)op; (void)fmt; (void)bias; }
void GXSetProjection(const void* mtx, u32 type) { GXLoadProjectionMtx(mtx, type); }
void GXSetProjectionv(const f32* p) {
    /* p[0] = type, p[1..6] = projection params - convert to 4x4 */
    memset(g_gx.projection_mtx, 0, sizeof(g_gx.projection_mtx));
    g_gx.projection_mtx[0][0] = p[1];
    g_gx.projection_mtx[0][2] = p[2];
    g_gx.projection_mtx[1][1] = p[3];
    g_gx.projection_mtx[1][2] = p[4];
    g_gx.projection_mtx[2][2] = p[5];
    g_gx.projection_mtx[2][3] = p[6];
    g_gx.projection_type = (int)p[0];
    if (g_gx.projection_type == GX_ORTHOGRAPHIC) {
        g_gx.projection_mtx[0][3] = p[2];
        g_gx.projection_mtx[0][2] = 0;
        g_gx.projection_mtx[1][3] = p[4];
        g_gx.projection_mtx[1][2] = 0;
    }
    DIRTY(PC_GX_DIRTY_PROJECTION);
}
void GXSetVtxDescv(const void* attrList) {
    const GXVtxDescList* list = (const GXVtxDescList*)attrList;
    if (list == NULL) {
        return;
    }

    for (; list->attr != GX_VA_NULL; list++) {
        GXSetVtxDesc(list->attr, list->type);
    }
}

void GXSetVtxAttrFmtv(u32 vtxfmt, const void* list) {
    const GXVtxAttrFmtList* fmt_list = (const GXVtxAttrFmtList*)list;
    if (fmt_list == NULL) {
        return;
    }

    for (; fmt_list->attr != GX_VA_NULL; fmt_list++) {
        GXSetVtxAttrFmt(vtxfmt, fmt_list->attr, fmt_list->cnt, fmt_list->type, fmt_list->frac);
    }
}
u32 GXGetNumXfbLines(u16 efbHeight, f32 yScale) { (void)yScale; return efbHeight; }
f32 GXGetYScaleFactor(u16 efbHeight, u16 xfbHeight) { return (f32)efbHeight / (f32)xfbHeight; }
void GXPeekZ(u16 x, u16 y, u32* z) { if (z) *z = 0xFFFFFF; (void)x; (void)y; }
void GXProject(f32 mx, f32 my, f32 mz, const f32 mtx[3][4], const f32* pm, const f32* vp,
               f32* sx, f32* sy, f32* sz) {
    /* Transform model→view→projection→screen */
    f32 vx = mtx[0][0]*mx + mtx[0][1]*my + mtx[0][2]*mz + mtx[0][3];
    f32 vy = mtx[1][0]*mx + mtx[1][1]*my + mtx[1][2]*mz + mtx[1][3];
    f32 vz = mtx[2][0]*mx + mtx[2][1]*my + mtx[2][2]*mz + mtx[2][3];
    /* Simplified: just return center of viewport */
    if (sx) *sx = vp ? vp[0] + vp[2] * 0.5f : 0;
    if (sy) *sy = vp ? vp[1] + vp[3] * 0.5f : 0;
    if (sz) *sz = 0;
    (void)vx; (void)vy; (void)vz; (void)pm;
}
void GXInitTexCacheRegion(void* region, u32 is32bmip, u32 tmemEven, u32 sizeEven,
                          u32 tmemOdd, u32 sizeOdd) {
    (void)region; (void)is32bmip; (void)tmemEven; (void)sizeEven; (void)tmemOdd; (void)sizeOdd;
}
u16 GXGetTexObjWidth(const void* obj) {
    const PCTexObj* tex = (const PCTexObj*)obj;
    return tex ? tex->width : 0;
}
u16 GXGetTexObjHeight(const void* obj) {
    const PCTexObj* tex = (const PCTexObj*)obj;
    return tex ? tex->height : 0;
}
u32 GXGetTexObjTlut(const void* obj) {
    const PCTexObj* tex = (const PCTexObj*)obj;
    return tex ? tex->tlut_name : 0;
}
u32 GXGetTexObjWrapS(const void* obj) {
    const PCTexObj* tex = (const PCTexObj*)obj;
    return tex ? tex->wrap_s : 0;
}
u32 GXGetTexObjWrapT(const void* obj) {
    const PCTexObj* tex = (const PCTexObj*)obj;
    return tex ? tex->wrap_t : 0;
}

/* GD (Graphics Display list builder) */
GDLObj* __GDCurrentDL = NULL;
static bool s_gd_overflowed = false;
static u8 s_gd_overflow_scratch[1 << 20];
static GDLObj s_gd_overflow_dl;

void GDInitGDLObj(GDLObj* dl, void* buf, u32 size) {
    if (dl == NULL) {
        return;
    }
    dl->start = (u8*)buf;
    dl->ptr = (u8*)buf;
    dl->top = (u8*)buf + size;
    dl->length = size;
}

void GDFlushCurrToMem(void) {}

void GDOverflowed(void) {
    s_gd_overflowed = true;
    static int s_overflow_log_count = 0;
    if (s_overflow_log_count++ < 32) {
        fprintf(stderr, "[GD] display list overflow\n");
    }
    if (__GDCurrentDL != NULL) {
        s_gd_overflow_dl.start = s_gd_overflow_scratch;
        s_gd_overflow_dl.ptr = s_gd_overflow_scratch;
        s_gd_overflow_dl.top = s_gd_overflow_scratch + sizeof(s_gd_overflow_scratch);
        s_gd_overflow_dl.length = sizeof(s_gd_overflow_scratch);
        __GDCurrentDL = &s_gd_overflow_dl;
    }
}

void GDPadCurr32(void) {
    if (__GDCurrentDL == NULL || __GDCurrentDL->ptr == NULL) {
        return;
    }
    uintptr_t n = (uintptr_t)__GDCurrentDL->ptr & 0x1F;
    if (n != 0) {
        for (; n < 32; n++) {
            *__GDCurrentDL->ptr++ = 0;
        }
    }
}

void GDSetArray(GXAttr attr, void* base, u8 stride) { GXSetArray(attr, base, stride); }
void GDSetArrayRaw(GXAttr attr, u32 addr, u8 stride) { GXSetArray(attr, (void*)(uintptr_t)addr, stride); }
void GDSetVtxDescv(const GXVtxDescList* attrList) { GXSetVtxDescv(attrList); }
void pc_gd_clear_overflow_flag(void) { s_gd_overflowed = false; }
int pc_gd_consume_overflow_flag(void) {
    int had_overflow = s_gd_overflowed ? 1 : 0;
    s_gd_overflowed = false;
    return had_overflow;
}

} /* extern "C" */

/* GF (Graphics Framework - Wii extension)
 * GF functions have C++ linkage (no extern "C" in their headers). */
#include <revolution/gx/GXEnum.h>
#include <revolution/gx/GXStruct.h>

void GFSetBlendModeEtc(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, GXLogicOp logic, u8 colorUpd, u8 alphaUpd, u8 ditherEn) {
    (void)type; (void)src; (void)dst; (void)logic; (void)colorUpd; (void)alphaUpd; (void)ditherEn;
}
void GFSetChanAmbColor(GXChannelID chan, GXColor color) { GXSetChanAmbColor(chan, *(u32*)&color); }
void GFSetFog(GXFogType type, f32 startZ, f32 endZ, f32 nearZ, f32 farZ, GXColor color) {
    GXSetFog(type, startZ, endZ, nearZ, farZ, *(u32*)&color);
}
void GFSetGenMode2(u8 nTexGens, u8 nChans, u8 nTevStages, u8 nIndStages, GXCullMode cullMode) {
    GXSetNumTexGens(nTexGens); GXSetNumChans(nChans); GXSetNumTevStages(nTevStages);
    GXSetNumIndStages(nIndStages); GXSetCullMode(cullMode);
}
void GFSetTevColorS10(GXTevRegID reg, GXColorS10 color) { GXSetTevColorS10(reg, *(u32*)&color); }
void GFSetZMode(u8 enable, GXCompare func, u8 updateEnable) { GXSetZMode(enable, func, updateEnable); }
