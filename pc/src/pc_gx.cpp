/* pc_gx.cpp - GX graphics API implementation (OpenGL 3.3 backend)
 * Phase 3: Full rendering pipeline with state tracking, uniform uploads,
 * vertex buffering, and GL draw calls. */
#include "pc_gx_internal.h"
#include <dolphin/gx/GXEnum.h>
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

/* ============================================================
 * Init / Shutdown
 * ============================================================ */
void pc_gx_init(void) {
    memset(&g_gx, 0, sizeof(g_gx));
    g_gx.dirty = PC_GX_DIRTY_ALL;
    g_gx.color_update_enable = 1;
    g_gx.alpha_update_enable = 1;
    g_gx.z_compare_enable = 1;
    g_gx.z_compare_func = GX_LEQUAL;
    g_gx.z_update_enable = 1;
    g_gx.alpha_comp0 = GX_ALWAYS;
    g_gx.alpha_comp1 = GX_ALWAYS;
    g_gx.num_chans = 1;

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

    /* Default state — match GX defaults (cull none, depth test on) */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);  /* GX default is GX_CULL_NONE */

    pc_gx_tev_init();
    pc_gx_texture_init();
    fprintf(stderr, "[PC] GX initialized (max %d verts, %d TEV stages)\n",
           PC_GX_MAX_VERTS, PC_GX_MAX_TEV_STAGES);
}

void pc_gx_shutdown(void) {
    pc_gx_tev_shutdown();
    pc_gx_texture_shutdown();
    if (g_gx.ebo) glDeleteBuffers(1, &g_gx.ebo);
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
        snprintf(name, sizeof(name), "u_use_texture%d", i);
        g_gx.uloc.use_texture[i] = glGetUniformLocation(shader, name);
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
        snprintf(name, sizeof(name), "u_tev%d_color_in", i);
        g_gx.uloc.tev_color_in[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev%d_alpha_in", i);
        g_gx.uloc.tev_alpha_in[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev%d_color_op", i);
        g_gx.uloc.tev_color_op[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev%d_alpha_op", i);
        g_gx.uloc.tev_alpha_op[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev%d_tc_src", i);
        g_gx.uloc.tev_tc_src[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev%d_bsc", i);
        g_gx.uloc.tev_bsc[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev%d_out", i);
        g_gx.uloc.tev_out[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev%d_swap", i);
        g_gx.uloc.tev_swap[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev%d_ind_cfg", i);
        g_gx.uloc.tev_ind_cfg[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_tev%d_ind_wrap", i);
        g_gx.uloc.tev_ind_wrap[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_ind_mtx_r0[%d]", i);
        g_gx.uloc.ind_mtx_r0[i] = glGetUniformLocation(shader, name);
        snprintf(name, sizeof(name), "u_ind_mtx_r1[%d]", i);
        g_gx.uloc.ind_mtx_r1[i] = glGetUniformLocation(shader, name);
    }
    /* ksel uses array indexing matching the shader */
    g_gx.uloc.tev_ksel = glGetUniformLocation(shader, "u_tev_ksel[0]");
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
        glColorMask(g_gx.color_update_enable ? GL_TRUE : GL_FALSE,
                    g_gx.color_update_enable ? GL_TRUE : GL_FALSE,
                    g_gx.color_update_enable ? GL_TRUE : GL_FALSE,
                    g_gx.alpha_update_enable ? GL_TRUE : GL_FALSE);
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

/* Convert GX projection to GL 4x4 column-major */
static void proj_to_gl44(const float src[4][4], int type, float dst[16]) {
    if (type == GX_PERSPECTIVE) {
        /* GX perspective: src is 4x4 but stored differently from GL */
        dst[ 0] = src[0][0]; dst[ 1] = 0;          dst[ 2] = 0;           dst[ 3] = 0;
        dst[ 4] = 0;         dst[ 5] = src[1][1];   dst[ 6] = 0;           dst[ 7] = 0;
        dst[ 8] = src[0][2]; dst[ 9] = src[1][2];   dst[10] = src[2][2];   dst[11] = -1.0f;
        dst[12] = 0;         dst[13] = 0;           dst[14] = src[2][3];   dst[15] = 0;
    } else {
        /* GX orthographic */
        dst[ 0] = src[0][0]; dst[ 1] = 0;          dst[ 2] = 0;           dst[ 3] = 0;
        dst[ 4] = 0;         dst[ 5] = src[1][1];   dst[ 6] = 0;           dst[ 7] = 0;
        dst[ 8] = 0;         dst[ 9] = 0;           dst[10] = src[2][2];   dst[11] = 0;
        dst[12] = src[0][3]; dst[13] = src[1][3];   dst[14] = src[2][3];   dst[15] = 1.0f;
    }
}

static void upload_uniforms(void) {
    unsigned int d = g_gx.dirty;

    if (d & PC_GX_DIRTY_PROJECTION) {
        float gl_proj[16];
        proj_to_gl44(g_gx.projection_mtx, g_gx.projection_type, gl_proj);
        if (g_gx.uloc.projection >= 0)
            glUniformMatrix4fv(g_gx.uloc.projection, 1, GL_FALSE, gl_proj);
    }

    if (d & PC_GX_DIRTY_MODELVIEW) {
        int idx = g_gx.current_mtx / 3;
        if (idx < 0 || idx >= 10) idx = 0;
        float gl_mv[16];
        mtx34_to_gl44(g_gx.pos_mtx[idx], gl_mv);
        if (g_gx.uloc.modelview >= 0)
            glUniformMatrix4fv(g_gx.uloc.modelview, 1, GL_FALSE, gl_mv);

        /* Normal matrix (3x3 upper-left of modelview, transposed inverse for uniform scale) */
        float nrm[9];
        nrm[0] = g_gx.nrm_mtx[idx][0][0]; nrm[1] = g_gx.nrm_mtx[idx][1][0]; nrm[2] = g_gx.nrm_mtx[idx][2][0];
        nrm[3] = g_gx.nrm_mtx[idx][0][1]; nrm[4] = g_gx.nrm_mtx[idx][1][1]; nrm[5] = g_gx.nrm_mtx[idx][2][1];
        nrm[6] = g_gx.nrm_mtx[idx][0][2]; nrm[7] = g_gx.nrm_mtx[idx][1][2]; nrm[8] = g_gx.nrm_mtx[idx][2][2];
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
            glUniform1i(g_gx.uloc.num_tev_stages, g_gx.num_tev_stages);

        for (int i = 0; i < g_gx.num_tev_stages && i < PC_GX_MAX_TEV_STAGES; i++) {
            PCGXTevStage* s = &g_gx.tev_stages[i];
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
        for (int i = 0; i < g_gx.num_tev_stages && i < PC_GX_MAX_TEV_STAGES; i++) {
            if (g_gx.uloc.tev_swap[i] >= 0)
                glUniform2i(g_gx.uloc.tev_swap[i], g_gx.tev_stages[i].ras_swap, g_gx.tev_stages[i].tex_swap);
        }
    }

    if (d & PC_GX_DIRTY_KONST) {
        if (g_gx.uloc.kcolor >= 0)
            glUniform4fv(g_gx.uloc.kcolor, 4, (float*)g_gx.tev_k_colors);
        if (g_gx.uloc.tev_ksel >= 0) {
            int ksel[PC_GX_MAX_TEV_STAGES * 3];
            for (int i = 0; i < PC_GX_MAX_TEV_STAGES; i++) {
                ksel[i * 3 + 0] = g_gx.tev_stages[i].k_color_sel;
                ksel[i * 3 + 1] = g_gx.tev_stages[i].k_alpha_sel;
                ksel[i * 3 + 2] = 0;
            }
            glUniform3iv(g_gx.uloc.tev_ksel, PC_GX_MAX_TEV_STAGES, ksel);
        }
    }

    if (d & PC_GX_DIRTY_ALPHA_CMP) {
        if (g_gx.uloc.alpha_comp0 >= 0) glUniform1i(g_gx.uloc.alpha_comp0, g_gx.alpha_comp0);
        if (g_gx.uloc.alpha_ref0 >= 0) glUniform1i(g_gx.uloc.alpha_ref0, g_gx.alpha_ref0);
        if (g_gx.uloc.alpha_op >= 0) glUniform1i(g_gx.uloc.alpha_op, g_gx.alpha_op);
        if (g_gx.uloc.alpha_comp1 >= 0) glUniform1i(g_gx.uloc.alpha_comp1, g_gx.alpha_comp1);
        if (g_gx.uloc.alpha_ref1 >= 0) glUniform1i(g_gx.uloc.alpha_ref1, g_gx.alpha_ref1);
    }

    if (d & PC_GX_DIRTY_LIGHTING) {
        int lit = g_gx.chan_ctrl_enable[0];
        int alit = (g_gx.num_chans > 0) ? g_gx.chan_ctrl_enable[1] : 0;
        if (g_gx.uloc.lighting_enabled >= 0) glUniform1i(g_gx.uloc.lighting_enabled, lit);
        if (g_gx.uloc.alpha_lighting_enabled >= 0) glUniform1i(g_gx.uloc.alpha_lighting_enabled, alit);
        if (g_gx.uloc.num_chans >= 0) glUniform1i(g_gx.uloc.num_chans, g_gx.num_chans);
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

    if (d & PC_GX_DIRTY_TEXTURES) {
        for (int i = 0; i < 8; i++) {
            int has_tex = (g_gx.gl_textures[i] != 0);
            if (g_gx.uloc.use_texture[i] >= 0)
                glUniform1i(g_gx.uloc.use_texture[i], has_tex ? 1 : 0);
            if (has_tex) {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, g_gx.gl_textures[i]);
                if (g_gx.uloc.texture[i] >= 0)
                    glUniform1i(g_gx.uloc.texture[i], i);
            }
        }
    }

    if (d & PC_GX_DIRTY_INDIRECT) {
        if (g_gx.uloc.num_ind_stages >= 0)
            glUniform1i(g_gx.uloc.num_ind_stages, g_gx.num_ind_stages);
        for (int i = 0; i < g_gx.num_tev_stages && i < PC_GX_MAX_TEV_STAGES; i++) {
            PCGXTevStage* s = &g_gx.tev_stages[i];
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
}

/* ============================================================
 * Vertex flush — the core rendering function
 * ============================================================ */
static int s_draw_call_count = 0;

void pc_gx_flush_vertices(void) {
    int count = g_gx.current_vertex_idx;
    if (count <= 0) {
        g_gx.vertex_count = 0;
        g_gx.current_vertex_idx = 0;
        return;
    }

    s_draw_call_count++;

    /* Select shader and cache uniforms if changed */
    GLuint shader = pc_gx_tev_get_shader(&g_gx);
    if (shader != g_gx.current_shader) {
        glUseProgram(shader);
        g_gx.current_shader = shader;
        pc_gx_cache_uniform_locations(shader);
    }
    /* Force full state upload every draw call until rendering is stable */
    g_gx.dirty = PC_GX_DIRTY_ALL;

    /* Apply GL state */
    apply_gl_state();

    /* Upload uniforms */
    upload_uniforms();
    g_gx.dirty = 0;

    /* Upload vertex data */
    glBindVertexArray(g_gx.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_gx.vbo);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(PCGXVertex), g_gx.vertex_buffer, GL_STREAM_DRAW);

    /* Viewport: GX viewport is in EFB coords, map to window */
    float sx = (float)g_pc_window_w / (float)PC_GC_WIDTH;
    float sy = (float)g_pc_window_h / (float)PC_GC_HEIGHT;
    glViewport((int)(g_gx.viewport[0] * sx),
               (int)((PC_GC_HEIGHT - g_gx.viewport[1] - g_gx.viewport[3]) * sy),
               (int)(g_gx.viewport[2] * sx),
               (int)(g_gx.viewport[3] * sy));

    /* Scissor */
    if (g_gx.scissor[2] > 0 && g_gx.scissor[3] > 0) {
        glEnable(GL_SCISSOR_TEST);
        glScissor((int)(g_gx.scissor[0] * sx),
                  (int)((PC_GC_HEIGHT - g_gx.scissor[1] - g_gx.scissor[3]) * sy),
                  (int)(g_gx.scissor[2] * sx),
                  (int)(g_gx.scissor[3] * sy));
    } else {
        glDisable(GL_SCISSOR_TEST);
    }

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

    if (g_gx.current_primitive == GX_QUADS) {
        int num_quads = count / 4;
        int num_indices = num_quads * 6;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gx.ebo);
        glDrawElements(gl_prim, num_indices, GL_UNSIGNED_SHORT, 0);
    } else {
        glDrawArrays(gl_prim, 0, count);
    }

    g_gx.vertex_count = 0;
    g_gx.current_vertex_idx = 0;
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
    if (g_gx.vertex_pending) commit_vertex();
    g_gx.current_vertex.position[0] = x;
    g_gx.current_vertex.position[1] = y;
    g_gx.current_vertex.position[2] = z;
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
    const u8* base = (const u8*)g_gx.array_base[GX_VA_POS];
    u8 stride = g_gx.array_stride[GX_VA_POS];
    if (base && stride >= 12) {
        const f32* p = (const f32*)(base + idx * stride);
        GXPosition3f32(p[0], p[1], p[2]);
    } else {
        GXPosition3f32(0, 0, 0);
    }
}
void GXPosition1x8(const u8 idx) { GXPosition1x16(idx); }

/* GXNormal */
void GXNormal3f32(const f32 x, const f32 y, const f32 z) {
    g_gx.current_vertex.normal[0] = x;
    g_gx.current_vertex.normal[1] = y;
    g_gx.current_vertex.normal[2] = z;
}
void GXNormal3s16(const s16 x, const s16 y, const s16 z) {
    GXNormal3f32(x / 32767.0f, y / 32767.0f, z / 32767.0f);
}
void GXNormal3s8(const s8 x, const s8 y, const s8 z) {
    GXNormal3f32(x / 127.0f, y / 127.0f, z / 127.0f);
}
void GXNormal1x16(const u16 idx) {
    const u8* base = (const u8*)g_gx.array_base[GX_VA_NRM];
    u8 stride = g_gx.array_stride[GX_VA_NRM];
    if (base && stride >= 12) {
        const f32* n = (const f32*)(base + idx * stride);
        GXNormal3f32(n[0], n[1], n[2]);
    }
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
    const u8* base = (const u8*)g_gx.array_base[GX_VA_CLR0];
    u8 stride = g_gx.array_stride[GX_VA_CLR0];
    if (base && stride >= 4) {
        const u8* c = base + idx * stride;
        GXColor4u8(c[0], c[1], c[2], c[3]);
    }
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
    const u8* base = (const u8*)g_gx.array_base[GX_VA_TEX0];
    u8 stride = g_gx.array_stride[GX_VA_TEX0];
    if (base && stride >= 8) {
        const f32* t = (const f32*)(base + idx * stride);
        GXTexCoord2f32(t[0], t[1]);
    }
}
void GXTexCoord1x8(const u8 idx) { GXTexCoord1x16(idx); }

/* GXMatrixIndex */
void GXMatrixIndex1u8(const u8 x) { (void)x; }

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
void GXCallDisplayList(const void* list, u32 nbytes) {
    /* TODO Phase 3c: GX display list interpreter */
    (void)list; (void)nbytes;
}

/* Vertex format */
void GXSetVtxDesc(u32 attr, u32 type) { if (attr < PC_GX_MAX_ATTR) g_gx.vtx_desc[attr] = type; }
void GXClearVtxDesc(void) { memset(g_gx.vtx_desc, 0, sizeof(g_gx.vtx_desc)); }
void GXSetVtxAttrFmt(u32 vtxfmt, u32 attr, u32 cnt, u32 type, u32 frac) {
    (void)vtxfmt; (void)cnt; (void)type;
    /* Store fractional bits for texcoord and position fixed-point conversion */
    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
        g_gx.tc_frac[attr - GX_VA_TEX0] = frac;
    } else if (attr == GX_VA_POS) {
        g_gx.pos_frac = frac;
    }
}
void GXSetArray(u32 attr, const void* base, u8 stride) {
    if (attr < PC_GX_MAX_ATTR) { g_gx.array_base[attr] = base; g_gx.array_stride[attr] = stride; }
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
void GXLoadTexMtxImm(const void* mtx, u32 id, u32 type) {
    (void)type;
    if (id / 3 < 10) memcpy(g_gx.tex_mtx[id / 3], mtx, 48);
    DIRTY(PC_GX_DIRTY_TEXGEN);
}
void GXSetCurrentMtx(u32 id) { g_gx.current_mtx = id; DIRTY(PC_GX_DIRTY_MODELVIEW); }
void GXLoadProjectionMtx(const void* mtx, u32 type) {
    memcpy(g_gx.projection_mtx, mtx, 64);
    g_gx.projection_type = type;
    DIRTY(PC_GX_DIRTY_PROJECTION);
}

/* Viewport & scissor */
void GXSetViewport(f32 x, f32 y, f32 w, f32 h, f32 nearz, f32 farz) {
    g_gx.viewport[0] = x; g_gx.viewport[1] = y;
    g_gx.viewport[2] = w; g_gx.viewport[3] = h;
    g_gx.viewport[4] = nearz; g_gx.viewport[5] = farz;
}
void GXSetViewportJitter(f32 x, f32 y, f32 w, f32 h, f32 nearz, f32 farz, u32 field) {
    (void)field; GXSetViewport(x, y, w, h, nearz, farz);
}
void GXSetScissor(u32 left, u32 top, u32 w, u32 h) {
    g_gx.scissor[0] = left; g_gx.scissor[1] = top;
    g_gx.scissor[2] = w; g_gx.scissor[3] = h;
}

/* TEV */
void GXSetNumTevStages(u32 nStages) { g_gx.num_tev_stages = nStages; DIRTY(PC_GX_DIRTY_TEV_STAGES); }
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

void GXLoadTexObj(void* obj, u32 mapID) {
    PCTexObj* tex = (PCTexObj*)obj;
    if (!tex || !tex->image_ptr || mapID >= 8) return;
    if (g_pc_verbose && s_draw_call_count < 5) {
        fprintf(stderr, "[GX] GXLoadTexObj: map=%d fmt=%d %dx%d ptr=%p\n",
                mapID, tex->format, tex->width, tex->height, tex->image_ptr);
    }

    /* Check texture cache first */
    const void* tlut_ptr = NULL;
    int tlut_fmt = 0, tlut_cnt = 0;
    if (tex->tlut_name < 16 && g_gx.tlut[tex->tlut_name].data) {
        tlut_ptr = g_gx.tlut[tex->tlut_name].data;
        tlut_fmt = g_gx.tlut[tex->tlut_name].format;
        tlut_cnt = g_gx.tlut[tex->tlut_name].n_entries;
    }

    GLuint gl_tex = pc_gx_texture_cache_lookup(tex->image_ptr, tex->width, tex->height,
                                                tex->format, tex->tlut_name, tlut_ptr);
    if (gl_tex == 0) {
        /* Decode and upload */
        gl_tex = pc_gx_texture_decode_and_upload(tex->image_ptr, tex->width, tex->height,
                                                  tex->format, (void*)tlut_ptr, tlut_fmt, tlut_cnt);
        if (gl_tex) {
            pc_gx_texture_cache_insert(tex->image_ptr, tex->width, tex->height,
                                       tex->format, tex->tlut_name, tlut_ptr, gl_tex);
        }
    }

    if (gl_tex) {
        g_gx.gl_textures[mapID] = gl_tex;
        g_gx.tex_obj_w[mapID] = tex->width;
        g_gx.tex_obj_h[mapID] = tex->height;
        g_gx.tex_obj_fmt[mapID] = tex->format;
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
void GXInvalidateTexAll(void) { pc_gx_texture_cache_invalidate(); }
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
    if (chan < 2) {
        g_gx.chan_mat_color[chan][0] = GXCOLOR_R(color) / 255.0f;
        g_gx.chan_mat_color[chan][1] = GXCOLOR_G(color) / 255.0f;
        g_gx.chan_mat_color[chan][2] = GXCOLOR_B(color) / 255.0f;
        g_gx.chan_mat_color[chan][3] = GXCOLOR_A(color) / 255.0f;
        DIRTY(PC_GX_DIRTY_LIGHTING);
    }
}
void GXSetChanAmbColor(u32 chan, u32 color) {
    if (chan < 2) {
        g_gx.chan_amb_color[chan][0] = GXCOLOR_R(color) / 255.0f;
        g_gx.chan_amb_color[chan][1] = GXCOLOR_G(color) / 255.0f;
        g_gx.chan_amb_color[chan][2] = GXCOLOR_B(color) / 255.0f;
        g_gx.chan_amb_color[chan][3] = GXCOLOR_A(color) / 255.0f;
        DIRTY(PC_GX_DIRTY_LIGHTING);
    }
}
void GXSetChanCtrl(u32 chan, u32 enable, u32 ambSrc, u32 matSrc, u32 lightMask, u32 diffFn, u32 attnFn) {
    if (chan < 4) {
        g_gx.chan_ctrl_enable[chan] = enable;
        g_gx.chan_ctrl_amb_src[chan] = ambSrc;
        g_gx.chan_ctrl_mat_src[chan] = matSrc;
        g_gx.chan_ctrl_light_mask[chan] = lightMask;
        g_gx.chan_ctrl_diff_fn[chan] = diffFn;
        g_gx.chan_ctrl_attn_fn[chan] = attnFn;
        DIRTY(PC_GX_DIRTY_LIGHTING);
    }
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
    if (id >= 8 || !light) return;
    f32* obj = (f32*)light;
    u32* uobj = (u32*)light;
    g_gx.lights[id].pos[0] = obj[4];
    g_gx.lights[id].pos[1] = obj[5];
    g_gx.lights[id].pos[2] = obj[6];
    u32 c = uobj[3];
    g_gx.lights[id].color[0] = GXCOLOR_R(c) / 255.0f;
    g_gx.lights[id].color[1] = GXCOLOR_G(c) / 255.0f;
    g_gx.lights[id].color[2] = GXCOLOR_B(c) / 255.0f;
    g_gx.lights[id].color[3] = GXCOLOR_A(c) / 255.0f;
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
    g_gx.clear_color[0] = GXCOLOR_R(color) / 255.0f;
    g_gx.clear_color[1] = GXCOLOR_G(color) / 255.0f;
    g_gx.clear_color[2] = GXCOLOR_B(color) / 255.0f;
    g_gx.clear_color[3] = GXCOLOR_A(color) / 255.0f;
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
void GXSetDispCopyYScale(f32 yscale) { (void)yscale; }
u32 GXSetDispCopyFrame2Field(u32 mode) { (void)mode; return 0; }
void GXSetCopyClamp(u32 clamp) { (void)clamp; }
static int s_copy_disp_count = 0;
void GXCopyDisp(void* dest, u32 clear) {
    (void)dest;
    if (g_pc_verbose && s_copy_disp_count < 10) {
        fprintf(stderr, "[GX] GXCopyDisp clear=%d color=(%.2f,%.2f,%.2f,%.2f)\n",
                clear, g_gx.clear_color[0], g_gx.clear_color[1], g_gx.clear_color[2], g_gx.clear_color[3]);
        s_copy_disp_count++;
    }
#ifndef _WIN32
    extern int pthread_main_np(void);
    if (!pthread_main_np()) return; /* GL only on main thread */
#endif
    if (clear) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* (test triangle removed) */
    }
}
void GXSetPixelFmt(u32 pix_fmt, u32 z_fmt) { (void)pix_fmt; (void)z_fmt; }

/* Misc */
void GXSetLineWidth(u8 width, u32 texOffsets) { (void)texOffsets; if (width > 0) glLineWidth(width / 6.0f); }
void GXSetPointSize(u8 size, u32 texOffsets) { (void)texOffsets; if (size > 0) glPointSize(size / 6.0f); }
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
void GXDrawDone(void) {}
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
void GXSetVtxDescv(const void* attrList) { (void)attrList; /* TODO */ }
void GXSetVtxAttrFmtv(u32 vtxfmt, const void* list) { (void)vtxfmt; (void)list; /* TODO */ }
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
void* __GDCurrentDL = NULL;
void GDInitGDLObj(void* dl, void* buf, u32 size) { (void)dl; (void)buf; (void)size; }
void GDFlushCurrToMem(void) {}
int GDOverflowed(void) { return 0; }
void GDPadCurr32(void) {}
void GDSetArray(u32 attr, const void* base, u8 stride) { (void)attr; (void)base; (void)stride; }
void GDSetArrayRaw(u32 attr, u32 addr) { (void)attr; (void)addr; }
void GDSetVtxDescv(const void* attrList) { (void)attrList; }

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
