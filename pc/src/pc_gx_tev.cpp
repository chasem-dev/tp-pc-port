/* pc_gx_tev.cpp - TEV shader management */
#include "pc_gx_internal.h"
#include <dolphin/gx/GXEnum.h>
#include <dolphin/vi.h>
#include <cstdlib>

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

    /* Verify attribute locations match expectations */
    GLint pos_loc = glGetAttribLocation(prog, "a_position");
    GLint nrm_loc = glGetAttribLocation(prog, "a_normal");
    GLint col_loc = glGetAttribLocation(prog, "a_color0");
    GLint tc_loc = glGetAttribLocation(prog, "a_texcoord0");
    fprintf(stderr, "[TEV] Attribute locs: pos=%d nrm=%d col=%d tc=%d\n",
            pos_loc, nrm_loc, col_loc, tc_loc);
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
    "uniform vec4 u_simple_kmod;\n"
    "uniform int u_simple_use_kmod;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 tex = vec4(1.0);\n"
    "    if (u_use_texture0 != 0) tex = texture(u_texture0, v_texcoord0);\n"
    "    vec4 col = tex * v_color;\n"
    "    if (u_simple_use_kmod != 0) col.rgb *= u_simple_kmod.rgb;\n"
    "    fragColor = col;\n"
    "}\n";

static bool pc_force_simple_shader() {
    static int cached = -1;
    if (cached != -1) {
        return cached != 0;
    }
    const char* env = getenv("TP_FORCE_SIMPLE_SHADER");
    /* Default to full TEV shader. The Metal pipeline warmup pre-compiles
     * the needed state variants so runtime draws don't trigger the compiler.
     * Set TP_FORCE_SIMPLE_SHADER=1 to fall back to the passthrough shader. */
    cached = (env != NULL && atoi(env) != 0) ? 1 : 0;
    return cached != 0;
}

static bool use_simple_shader(const PCGXState* state) {
    (void)state;
    return pc_force_simple_shader();
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

    /* Try to load the full TEV shader from file — check multiple paths */
    static const char* shader_paths[][2] = {
        {"pc/shaders/default.vert",            "pc/shaders/default.frag"},
        {"pc/build/bin/shaders/default.vert",  "pc/build/bin/shaders/default.frag"},
        {"shaders/default.vert",               "shaders/default.frag"},
        {"build/bin/shaders/default.vert",     "build/bin/shaders/default.frag"},
    };
    for (int pi = 0; pi < 4 && !default_shader; pi++) {
        default_shader = load_shader_from_file(shader_paths[pi][0], shader_paths[pi][1]);
        if (default_shader) {
            fprintf(stderr, "[TEV] Full TEV shader loaded from '%s'\n", shader_paths[pi][0]);
        }
    }
    if (!default_shader) {
        fprintf(stderr, "[TEV] WARNING: shader not found, falling back to simple\n");
        default_shader = simple_shader;
    }
    glUseProgram(default_shader);
    pc_gx_cache_uniform_locations(default_shader);
    g_gx.dirty = PC_GX_DIRTY_ALL;

    fprintf(stderr, "[TEV] Shader ready\n");
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

GLuint pc_gx_tev_get_simple_shader(void) {
    return simple_shader;
}

GLuint pc_gx_tev_get_shader(PCGXState* state) {
    /* Always use full TEV shader — the boot simple budget was a workaround
     * for the Metal crash (now fixed by disabling VI callbacks). The simple
     * shader doesn't handle TEV color ops needed for logo rendering. */
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
