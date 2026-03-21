/* pc_gx_tev.cpp - TEV shader management */
#include "pc_gx_internal.h"

static GLuint default_shader = 0;

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

void pc_gx_tev_init(void) {
    /* Try to load from shader files — check multiple paths since CWD may vary */
    static const char* search_paths[] = {
        "shaders/default",          /* CWD = bin/ */
        "bin/shaders/default",      /* CWD = build/ */
        "../shaders/default",       /* CWD = bin/subdir */
        NULL
    };
    for (const char** p = search_paths; *p && !default_shader; p++) {
        char vpath[256], fpath[256];
        snprintf(vpath, sizeof(vpath), "%s.vert", *p);
        snprintf(fpath, sizeof(fpath), "%s.frag", *p);
        default_shader = load_shader_from_file(vpath, fpath);
    }

    if (!default_shader) {
        /* Use fallback */
        GLuint vs = compile_shader(GL_VERTEX_SHADER, fallback_vert);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fallback_frag);
        default_shader = glCreateProgram();
        glAttachShader(default_shader, vs);
        glAttachShader(default_shader, fs);
        glLinkProgram(default_shader);
        glDeleteShader(vs);
        glDeleteShader(fs);
        fprintf(stderr, "[TEV] Using fallback shader\n");
    } else {
        fprintf(stderr, "[TEV] Loaded shaders from files\n");
    }

    glUseProgram(default_shader);
    pc_gx_cache_uniform_locations(default_shader);
    g_gx.dirty = PC_GX_DIRTY_ALL;

    fprintf(stderr, "[TEV] Shader ready\n");
    g_gx.current_shader = default_shader;
}

void pc_gx_tev_shutdown(void) {
    if (default_shader) { glDeleteProgram(default_shader); default_shader = 0; }
}

GLuint pc_gx_tev_get_shader(PCGXState* state) {
    (void)state;
    return default_shader;
}
