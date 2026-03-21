/* pc_gx_tev.cpp - TEV shader management */
#include "pc_gx_internal.h"
#include <dolphin/gx/GXEnum.h>

static GLuint default_shader = 0;
static GLuint simple_shader = 0;

static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "[TEV] Shader compile error: %s\n", log);
    }
    return shader;
}

static GLuint load_shader_from_file(const char* vert_path, const char* frag_path) {
    /* Try loading from files first */
    FILE* vf = fopen(vert_path, "rb");
    FILE* ff = fopen(frag_path, "rb");

    if (!vf || !ff) {
        if (vf) fclose(vf);
        if (ff) fclose(ff);
        return 0;
    }

    fseek(vf, 0, SEEK_END); long vlen = ftell(vf); fseek(vf, 0, SEEK_SET);
    fseek(ff, 0, SEEK_END); long flen = ftell(ff); fseek(ff, 0, SEEK_SET);

    char* vsrc = (char*)malloc(vlen + 1);
    char* fsrc = (char*)malloc(flen + 1);
    fread(vsrc, 1, vlen, vf); vsrc[vlen] = 0;
    fread(fsrc, 1, flen, ff); fsrc[flen] = 0;
    fclose(vf); fclose(ff);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vsrc);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fsrc);
    free(vsrc); free(fsrc);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "a_position");
    glBindAttribLocation(prog, 1, "a_normal");
    glBindAttribLocation(prog, 2, "a_color0");
    glBindAttribLocation(prog, 3, "a_texcoord0");
    glLinkProgram(prog);

    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "[TEV] Shader link error: %s\n", log);
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

/* Fallback minimal shader */
static const char* fallback_vert =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNormal;\n"
    "layout(location=2) in vec4 aColor0;\n"
    "layout(location=3) in vec2 aTexCoord0;\n"
    "uniform mat4 u_projection;\n"
    "uniform mat4 u_modelview;\n"
    "out vec4 vColor;\n"
    "out vec2 vTexCoord;\n"
    "void main() {\n"
    "    gl_Position = u_projection * u_modelview * vec4(aPos, 1.0);\n"
    "    vColor = aColor0;\n"
    "    vTexCoord = aTexCoord0;\n"
    "}\n";

static const char* fallback_frag =
    "#version 330 core\n"
    "in vec4 vColor;\n"
    "in vec2 vTexCoord;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = vColor;\n"
    "}\n";

static const char* simple_vert =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_position;\n"
    "layout(location=1) in vec3 a_normal;\n"
    "layout(location=2) in vec4 a_color0;\n"
    "layout(location=3) in vec2 a_texcoord0;\n"
    "uniform mat4 u_projection;\n"
    "uniform mat4 u_modelview;\n"
    "out vec4 v_color;\n"
    "out vec2 v_texcoord0;\n"
    "void main() {\n"
    "    gl_Position = u_projection * u_modelview * vec4(a_position, 1.0);\n"
    "    v_color = a_color0;\n"
    "    v_texcoord0 = a_texcoord0;\n"
    "}\n";

static const char* simple_frag =
    "#version 330 core\n"
    "in vec4 v_color;\n"
    "in vec2 v_texcoord0;\n"
    "uniform sampler2D u_texture0;\n"
    "uniform int u_use_texture0;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 tex = vec4(1.0);\n"
    "    if (u_use_texture0 != 0) tex = texture(u_texture0, v_texcoord0);\n"
    "    fragColor = tex * v_color;\n"
    "}\n";

static bool use_simple_shader(const PCGXState* state) {
    if (state->current_primitive != GX_QUADS || state->num_ind_stages != 0) {
        return false;
    }

    if (state->num_tev_stages != 1 || state->num_tex_gens > 1) {
        return false;
    }

    const PCGXTevStage* stage0 = &state->tev_stages[0];
    if (stage0->tex_map > 0 || stage0->tex_coord > 0) {
        return false;
    }

    return true;
}

void pc_gx_tev_init(void) {
    /* Only compile the simple shader at init — defer the full TEV shader
     * to avoid triggering Metal's MemoryPoolDecay crash on macOS ARM64.
     * The full shader will be compiled on demand when needed. */
    {
        GLuint vs = compile_shader(GL_VERTEX_SHADER, simple_vert);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, simple_frag);
        simple_shader = glCreateProgram();
        glAttachShader(simple_shader, vs);
        glAttachShader(simple_shader, fs);
        glBindAttribLocation(simple_shader, 0, "a_position");
        glBindAttribLocation(simple_shader, 1, "a_normal");
        glBindAttribLocation(simple_shader, 2, "a_color0");
        glBindAttribLocation(simple_shader, 3, "a_texcoord0");
        glLinkProgram(simple_shader);
        glDeleteShader(vs);
        glDeleteShader(fs);
    }

    /* Use simple shader as default until full TEV is needed */
    default_shader = simple_shader;
    glUseProgram(default_shader);
    pc_gx_cache_uniform_locations(default_shader);
    g_gx.dirty = PC_GX_DIRTY_ALL;

    fprintf(stderr, "[TEV] Shader ready (simple only, full TEV deferred)\n");
    g_gx.current_shader = default_shader;
}

void pc_gx_tev_shutdown(void) {
    if (default_shader) { glDeleteProgram(default_shader); default_shader = 0; }
    if (simple_shader) { glDeleteProgram(simple_shader); simple_shader = 0; }
}

bool pc_gx_tev_is_simple_shader(GLuint shader) {
    return shader != 0 && shader == simple_shader;
}

GLuint pc_gx_tev_get_default_shader(void) {
    return default_shader;
}

GLuint pc_gx_tev_get_shader(PCGXState* state) {
    static int s_boot_simple_budget = 10000; /* use simple shader for entire boot */
    if (simple_shader && state->current_primitive == GX_QUADS && state->num_ind_stages == 0 &&
        s_boot_simple_budget > 0) {
        static int s_simple_logs = 0;
        if (s_simple_logs < 10) {
            fprintf(stderr,
                    "[TEV] boot simple_shader: tev=%d texgens=%d tex0_map=%d tex0_coord=%d prim=%u budget=%d\n",
                    state->num_tev_stages, state->num_tex_gens, state->tev_stages[0].tex_map,
                    state->tev_stages[0].tex_coord, state->current_primitive, s_boot_simple_budget);
            s_simple_logs++;
        }
        s_boot_simple_budget--;
        return simple_shader;
    }

    if (simple_shader && use_simple_shader(state)) {
        static int s_simple_logs = 0;
        if (s_simple_logs < 10) {
            fprintf(stderr,
                    "[TEV] simple_shader: tev=%d texgens=%d tex_map=%d tex_coord=%d prim=%u\n",
                    state->num_tev_stages, state->num_tex_gens, state->tev_stages[0].tex_map,
                    state->tev_stages[0].tex_coord, state->current_primitive);
            s_simple_logs++;
        }
        return simple_shader;
    }
    return default_shader;
}
