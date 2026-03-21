/* pc_gx_texture.cpp - GC texture format decoding and cache
 * Decodes all 10 GC texture formats from tile-based big-endian layout
 * to linear RGBA8 for OpenGL upload. */
#include "pc_gx_internal.h"
#include <dolphin/gx/GXEnum.h>

/* ============================================================
 * Texture cache
 * ============================================================ */
#define TEX_CACHE_SIZE 2048

typedef struct {
    uintptr_t data_ptr;
    u16 width, height;
    u32 format;
    u32 tlut_name;
    uintptr_t tlut_ptr;
    u32 tlut_hash;
    u32 data_hash;
    u32 wrap_s;
    u32 wrap_t;
    u32 min_filter;
    GLuint gl_tex;
} TexCacheEntry;

static TexCacheEntry s_tex_cache[TEX_CACHE_SIZE];
static int s_tex_cache_count = 0;

void pc_gx_texture_init(void) {
    memset(s_tex_cache, 0, sizeof(s_tex_cache));
    s_tex_cache_count = 0;
    fprintf(stderr, "[TEX] Texture cache initialized (%d entries)\n", TEX_CACHE_SIZE);
}

void pc_gx_texture_shutdown(void) {
    for (int i = 0; i < s_tex_cache_count; i++) {
        if (s_tex_cache[i].gl_tex) glDeleteTextures(1, &s_tex_cache[i].gl_tex);
    }
    s_tex_cache_count = 0;
}

void pc_gx_texture_cache_invalidate(void) {
    pc_gx_texture_shutdown();
    pc_gx_texture_init();
}

GLuint pc_gx_texture_cache_lookup(void* data, int width, int height, int format,
                                   u32 tlut_name, const void* tlut_ptr,
                                   u32 tlut_hash, u32 data_hash,
                                   u32 wrap_s, u32 wrap_t, u32 min_filter) {
    uintptr_t dp = (uintptr_t)data;
    uintptr_t tp = (uintptr_t)tlut_ptr;
    for (int i = 0; i < s_tex_cache_count; i++) {
        if (s_tex_cache[i].data_ptr == dp &&
            s_tex_cache[i].width == width &&
            s_tex_cache[i].height == height &&
            s_tex_cache[i].format == (u32)format &&
            s_tex_cache[i].tlut_name == tlut_name &&
            s_tex_cache[i].tlut_ptr == tp &&
            s_tex_cache[i].tlut_hash == tlut_hash &&
            s_tex_cache[i].data_hash == data_hash &&
            s_tex_cache[i].wrap_s == wrap_s &&
            s_tex_cache[i].wrap_t == wrap_t &&
            s_tex_cache[i].min_filter == min_filter) {
            return s_tex_cache[i].gl_tex;
        }
    }
    return 0;
}

void pc_gx_texture_cache_insert(void* data, int width, int height, int format,
                                 u32 tlut_name, const void* tlut_ptr,
                                 u32 tlut_hash, u32 data_hash,
                                 u32 wrap_s, u32 wrap_t, u32 min_filter,
                                 GLuint gl_tex) {
    if (s_tex_cache_count >= TEX_CACHE_SIZE) {
        /* Evict oldest half */
        int half = TEX_CACHE_SIZE / 2;
        for (int i = 0; i < half; i++) {
            if (s_tex_cache[i].gl_tex) {
                for (int stage = 0; stage < 8; stage++) {
                    if (g_gx.gl_textures[stage] == s_tex_cache[i].gl_tex) {
                        g_gx.gl_textures[stage] = 0;
                    }
                }
                glDeleteTextures(1, &s_tex_cache[i].gl_tex);
            }
        }
        memmove(s_tex_cache, s_tex_cache + half, half * sizeof(TexCacheEntry));
        s_tex_cache_count = half;
    }
    TexCacheEntry* e = &s_tex_cache[s_tex_cache_count++];
    e->data_ptr = (uintptr_t)data;
    e->width = width;
    e->height = height;
    e->format = format;
    e->tlut_name = tlut_name;
    e->tlut_ptr = (uintptr_t)tlut_ptr;
    e->tlut_hash = tlut_hash;
    e->data_hash = data_hash;
    e->wrap_s = wrap_s;
    e->wrap_t = wrap_t;
    e->min_filter = min_filter;
    e->gl_tex = gl_tex;
}

static int gc_format_bpp(u32 format) {
    switch (format) {
        case GX_TF_I4:
        case GX_TF_C4:
        case GX_TF_CMPR:
            return 4;
        case GX_TF_I8:
        case GX_TF_IA4:
        case GX_TF_C8:
            return 8;
        case GX_TF_IA8:
        case GX_TF_RGB565:
        case GX_TF_RGB5A3:
        case GX_TF_C14X2:
            return 16;
        case GX_TF_RGBA8:
            return 32;
        default:
            return 8;
    }
}

static u32 fnv1a_hash(const u8* data, int len, u32 seed = 0x811c9dc5u) {
    u32 h = seed;
    for (int i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x01000193u;
    }
    return h;
}

u32 pc_gx_texture_data_hash(const void* data, int width, int height, u32 format) {
    if (!data || width <= 0 || height <= 0) {
        return 0;
    }

    int data_size = (width * height * gc_format_bpp(format)) / 8;
    if (data_size <= 0) {
        return 0;
    }

    const u8* p = static_cast<const u8*>(data);
    if (data_size <= 512) {
        return fnv1a_hash(p, data_size);
    }

    u32 h = fnv1a_hash(p, 256);
    return fnv1a_hash(p + data_size - 256, 256, h);
}

u32 pc_gx_tlut_hash(const void* data, int tlut_fmt, int n_entries) {
    if (!data || n_entries <= 0) {
        return 0;
    }

    int bytes = n_entries * 2;
    if (bytes > 512) {
        bytes = 512;
    }

    u32 h = fnv1a_hash(static_cast<const u8*>(data), bytes);
    h ^= (u32)(tlut_fmt & 0xFF);
    h *= 0x01000193u;
    h ^= (u32)(n_entries & 0xFFFF);
    h *= 0x01000193u;
    return h;
}

/* ============================================================
 * Big-endian read helpers
 * ============================================================ */
static inline u16 read_be16(const u8* p) {
    return (u16)((p[0] << 8) | p[1]);
}

/* ============================================================
 * TLUT palette decoding
 * ============================================================ */
static void decode_palette(const void* tlut_data, int tlut_fmt, int n_entries,
                           u8 palette[256][4]) {
    if (!tlut_data || n_entries <= 0) {
        /* Default: grayscale ramp */
        for (int i = 0; i < 256; i++) {
            palette[i][0] = palette[i][1] = palette[i][2] = (u8)i;
            palette[i][3] = 255;
        }
        return;
    }
    const u8* src = (const u8*)tlut_data;
    for (int i = 0; i < n_entries && i < 256; i++) {
        u16 val = read_be16(src + i * 2);
        if (tlut_fmt == 0) {
            /* GX_TL_IA8: high=intensity, low=alpha */
            u8 intensity = (val >> 8) & 0xFF;
            u8 alpha = val & 0xFF;
            palette[i][0] = palette[i][1] = palette[i][2] = intensity;
            palette[i][3] = alpha;
        } else if (tlut_fmt == 1) {
            /* GX_TL_RGB565 */
            u8 r = (u8)(((val >> 11) & 0x1F) * 255 / 31);
            u8 g = (u8)(((val >> 5) & 0x3F) * 255 / 63);
            u8 b = (u8)((val & 0x1F) * 255 / 31);
            palette[i][0] = r; palette[i][1] = g; palette[i][2] = b; palette[i][3] = 255;
        } else {
            /* GX_TL_RGB5A3 */
            if (val & 0x8000) {
                /* RGB555 opaque */
                u8 r = (u8)(((val >> 10) & 0x1F) * 255 / 31);
                u8 g = (u8)(((val >> 5) & 0x1F) * 255 / 31);
                u8 b = (u8)((val & 0x1F) * 255 / 31);
                palette[i][0] = r; palette[i][1] = g; palette[i][2] = b; palette[i][3] = 255;
            } else {
                /* ARGB3444 */
                u8 a = (u8)(((val >> 12) & 0x7) * 255 / 7);
                u8 r = (u8)(((val >> 8) & 0xF) * 255 / 15);
                u8 g = (u8)(((val >> 4) & 0xF) * 255 / 15);
                u8 b = (u8)((val & 0xF) * 255 / 15);
                palette[i][0] = r; palette[i][1] = g; palette[i][2] = b; palette[i][3] = a;
            }
        }
    }
}

/* ============================================================
 * GC texture format decoders
 * All decode from tile-based big-endian to linear RGBA8.
 * ============================================================ */

/* I4: 8x8 blocks, 4bpp intensity */
static void decode_I4(const u8* src, u8* dst, int w, int h) {
    int bw = (w + 7) / 8, bh = (h + 7) / 8;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 4; x++) {
                    u8 byte = *src++;
                    int px0 = bx * 8 + x * 2;
                    int px1 = px0 + 1;
                    int py = by * 8 + y;
                    if (px0 < w && py < h) {
                        u8 v = (byte >> 4) | (byte & 0xF0);
                        int idx = (py * w + px0) * 4;
                        dst[idx] = dst[idx+1] = dst[idx+2] = v; dst[idx+3] = 255;
                    }
                    if (px1 < w && py < h) {
                        u8 v = (byte & 0x0F) | ((byte & 0x0F) << 4);
                        int idx = (py * w + px1) * 4;
                        dst[idx] = dst[idx+1] = dst[idx+2] = v; dst[idx+3] = 255;
                    }
                }
            }
        }
    }
}

/* I8: 8x4 blocks, 8bpp intensity */
static void decode_I8(const u8* src, u8* dst, int w, int h) {
    int bw = (w + 7) / 8, bh = (h + 3) / 4;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 8; x++) {
                    u8 v = *src++;
                    int px = bx * 8 + x, py = by * 4 + y;
                    if (px < w && py < h) {
                        int idx = (py * w + px) * 4;
                        dst[idx] = dst[idx+1] = dst[idx+2] = v; dst[idx+3] = 255;
                    }
                }
            }
        }
    }
}

/* IA4: 8x4 blocks, 8bpp (4-bit intensity + 4-bit alpha) */
static void decode_IA4(const u8* src, u8* dst, int w, int h) {
    int bw = (w + 7) / 8, bh = (h + 3) / 4;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 8; x++) {
                    u8 byte = *src++;
                    int px = bx * 8 + x, py = by * 4 + y;
                    if (px < w && py < h) {
                        u8 intensity = (byte & 0x0F) | ((byte & 0x0F) << 4);
                        u8 alpha = (byte >> 4) | (byte & 0xF0);
                        int idx = (py * w + px) * 4;
                        dst[idx] = dst[idx+1] = dst[idx+2] = intensity; dst[idx+3] = alpha;
                    }
                }
            }
        }
    }
}

/* IA8: 4x4 blocks, 16bpp (8-bit alpha + 8-bit intensity) */
static void decode_IA8(const u8* src, u8* dst, int w, int h) {
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 4; x++) {
                    u8 alpha = *src++;
                    u8 intensity = *src++;
                    int px = bx * 4 + x, py = by * 4 + y;
                    if (px < w && py < h) {
                        int idx = (py * w + px) * 4;
                        dst[idx] = dst[idx+1] = dst[idx+2] = intensity; dst[idx+3] = alpha;
                    }
                }
            }
        }
    }
}

/* RGB565: 4x4 blocks, 16bpp */
static void decode_RGB565(const u8* src, u8* dst, int w, int h) {
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 4; x++) {
                    u16 val = read_be16(src); src += 2;
                    int px = bx * 4 + x, py = by * 4 + y;
                    if (px < w && py < h) {
                        u8 r = (u8)(((val >> 11) & 0x1F) * 255 / 31);
                        u8 g = (u8)(((val >> 5) & 0x3F) * 255 / 63);
                        u8 b = (u8)((val & 0x1F) * 255 / 31);
                        int idx = (py * w + px) * 4;
                        dst[idx] = r; dst[idx+1] = g; dst[idx+2] = b; dst[idx+3] = 255;
                    }
                }
            }
        }
    }
}

/* RGB5A3: 4x4 blocks, 16bpp (mode-dependent) */
static void decode_RGB5A3(const u8* src, u8* dst, int w, int h) {
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 4; x++) {
                    u16 val = read_be16(src); src += 2;
                    int px = bx * 4 + x, py = by * 4 + y;
                    if (px < w && py < h) {
                        u8 r, g, b, a;
                        if (val & 0x8000) {
                            /* RGB555 opaque */
                            r = (u8)(((val >> 10) & 0x1F) * 255 / 31);
                            g = (u8)(((val >> 5) & 0x1F) * 255 / 31);
                            b = (u8)((val & 0x1F) * 255 / 31);
                            a = 255;
                        } else {
                            /* ARGB3444 */
                            a = (u8)(((val >> 12) & 0x7) * 255 / 7);
                            r = (u8)(((val >> 8) & 0xF) * 255 / 15);
                            g = (u8)(((val >> 4) & 0xF) * 255 / 15);
                            b = (u8)((val & 0xF) * 255 / 15);
                        }
                        int idx = (py * w + px) * 4;
                        dst[idx] = r; dst[idx+1] = g; dst[idx+2] = b; dst[idx+3] = a;
                    }
                }
            }
        }
    }
}

/* RGBA8: 4x4 blocks, 32bpp (two passes: AR then GB) */
static void decode_RGBA8(const u8* src, u8* dst, int w, int h) {
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            /* First 32 bytes: AR pairs for 4x4 block */
            u8 ar[16][2];
            for (int i = 0; i < 16; i++) {
                ar[i][0] = *src++; /* A */
                ar[i][1] = *src++; /* R */
            }
            /* Next 32 bytes: GB pairs */
            for (int i = 0; i < 16; i++) {
                u8 g_val = *src++;
                u8 b_val = *src++;
                int y = i / 4, x = i % 4;
                int px = bx * 4 + x, py = by * 4 + y;
                if (px < w && py < h) {
                    int idx = (py * w + px) * 4;
                    dst[idx]   = ar[i][1]; /* R */
                    dst[idx+1] = g_val;    /* G */
                    dst[idx+2] = b_val;    /* B */
                    dst[idx+3] = ar[i][0]; /* A */
                }
            }
        }
    }
}

/* CI4: 8x8 blocks, 4bpp palette-indexed */
static void decode_CI4(const u8* src, u8* dst, int w, int h, u8 palette[256][4]) {
    int bw = (w + 7) / 8, bh = (h + 7) / 8;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 4; x++) {
                    u8 byte = *src++;
                    int idx0 = (byte >> 4) & 0xF;
                    int idx1 = byte & 0xF;
                    int px0 = bx * 8 + x * 2, px1 = px0 + 1;
                    int py = by * 8 + y;
                    if (px0 < w && py < h) {
                        int o = (py * w + px0) * 4;
                        memcpy(dst + o, palette[idx0], 4);
                    }
                    if (px1 < w && py < h) {
                        int o = (py * w + px1) * 4;
                        memcpy(dst + o, palette[idx1], 4);
                    }
                }
            }
        }
    }
}

/* CI8: 8x4 blocks, 8bpp palette-indexed */
static void decode_CI8(const u8* src, u8* dst, int w, int h, u8 palette[256][4]) {
    int bw = (w + 7) / 8, bh = (h + 3) / 4;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 8; x++) {
                    u8 idx = *src++;
                    int px = bx * 8 + x, py = by * 4 + y;
                    if (px < w && py < h) {
                        int o = (py * w + px) * 4;
                        memcpy(dst + o, palette[idx], 4);
                    }
                }
            }
        }
    }
}

/* CMPR/DXT1: 8x8 super-blocks of 2x2 sub-blocks (4x4 DXT1 each) */
static inline void decode_rgb565_pixel(u16 val, u8* r, u8* g, u8* b) {
    *r = (u8)(((val >> 11) & 0x1F) * 255 / 31);
    *g = (u8)(((val >> 5) & 0x3F) * 255 / 63);
    *b = (u8)((val & 0x1F) * 255 / 31);
}

static void decode_CMPR(const u8* src, u8* dst, int w, int h) {
    int bw = (w + 7) / 8, bh = (h + 7) / 8;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            /* 2x2 sub-blocks within this 8x8 super-block */
            for (int sb = 0; sb < 4; sb++) {
                int sbx = sb & 1, sby = sb >> 1;
                u16 c0 = read_be16(src); src += 2;
                u16 c1 = read_be16(src); src += 2;

                u8 pal[4][4]; /* RGBA */
                decode_rgb565_pixel(c0, &pal[0][0], &pal[0][1], &pal[0][2]); pal[0][3] = 255;
                decode_rgb565_pixel(c1, &pal[1][0], &pal[1][1], &pal[1][2]); pal[1][3] = 255;

                if (c0 > c1) {
                    for (int c = 0; c < 3; c++) {
                        pal[2][c] = (u8)((2 * pal[0][c] + pal[1][c]) / 3);
                        pal[3][c] = (u8)((pal[0][c] + 2 * pal[1][c]) / 3);
                    }
                    pal[2][3] = pal[3][3] = 255;
                } else {
                    for (int c = 0; c < 3; c++)
                        pal[2][c] = (u8)((pal[0][c] + pal[1][c]) / 2);
                    pal[2][3] = 255;
                    pal[3][0] = pal[3][1] = pal[3][2] = 0; pal[3][3] = 0; /* transparent */
                }

                /* 4 rows of 4 pixels, 2 bits each = 4 bytes */
                for (int y = 0; y < 4; y++) {
                    u8 row = *src++;
                    for (int x = 0; x < 4; x++) {
                        int pi = (row >> (6 - x * 2)) & 3;
                        int px = bx * 8 + sbx * 4 + x;
                        int py = by * 8 + sby * 4 + y;
                        if (px < w && py < h) {
                            int o = (py * w + px) * 4;
                            memcpy(dst + o, pal[pi], 4);
                        }
                    }
                }
            }
        }
    }
}

/* ============================================================
 * Main decode + upload function
 * ============================================================ */
GLuint pc_gx_texture_decode_and_upload(void* data, int width, int height, int format,
                                        void* tlut, int tlut_format, int tlut_count) {
    if (!data || width <= 0 || height <= 0) return 0;
    pc_platform_ensure_gl_context_current();

    /* Large textures (warning screen, logos) are now decoded normally */

    int size = width * height * 4;
    u8* pixels = (u8*)malloc(size);
    if (!pixels) return 0;
    memset(pixels, 0, size);

    const u8* src = (const u8*)data;
    u8 palette[256][4];

    /* Debug: dump first bytes of source data */
    {
        static int s_decode_count = 0;
        s_decode_count++;
        if (s_decode_count <= 5 && width > 4) {
            fprintf(stderr, "[TEX] decode #%d: fmt=%d %dx%d src bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    s_decode_count, format, width, height,
                    src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7],
                    src[8], src[9], src[10], src[11], src[12], src[13], src[14], src[15]);
        }
    }

    switch (format) {
        case 0x0: decode_I4(src, pixels, width, height); break;
        case 0x1: decode_I8(src, pixels, width, height); break;
        case 0x2: decode_IA4(src, pixels, width, height); break;
        case 0x3: decode_IA8(src, pixels, width, height); break;
        case 0x4: decode_RGB565(src, pixels, width, height); break;
        case 0x5: decode_RGB5A3(src, pixels, width, height); break;
        case 0x6: decode_RGBA8(src, pixels, width, height); break;
        case 0x8: /* CI4 */
            decode_palette(tlut, tlut_format, tlut_count > 0 ? tlut_count : 16, palette);
            decode_CI4(src, pixels, width, height, palette);
            break;
        case 0x9: /* CI8 */
            decode_palette(tlut, tlut_format, tlut_count > 0 ? tlut_count : 256, palette);
            decode_CI8(src, pixels, width, height, palette);
            break;
        case 0xA: /* C14X2 - treat as CI8 with larger palette */
            decode_palette(tlut, tlut_format, tlut_count > 0 ? tlut_count : 256, palette);
            decode_CI8(src, pixels, width, height, palette);
            break;
        case 0xE: decode_CMPR(src, pixels, width, height); break;
        default:
            /* Unknown format: magenta checkerboard */
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int idx = (y * width + x) * 4;
                    int checker = ((x / 4) + (y / 4)) & 1;
                    pixels[idx] = checker ? 255 : 128;
                    pixels[idx+1] = 0;
                    pixels[idx+2] = checker ? 255 : 128;
                    pixels[idx+3] = 255;
                }
            }
            break;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(pixels);

    return tex;
}
