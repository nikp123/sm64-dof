#ifdef ENABLE_OPENGL

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#ifdef __MINGW32__
#define FOR_WINDOWS 1
#else
#define FOR_WINDOWS 0
#endif

#if FOR_WINDOWS
#include <GL/glew.h>
#include "SDL.h"
#define GL_GLEXT_PROTOTYPES 1
#include "SDL_opengl.h"
#else
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengles2.h>
#endif

#include "gfx_cc.h"
#include "gfx_rendering_api.h"

struct ShaderProgram {
    uint32_t shader_id;
    GLuint opengl_program_id;
    uint8_t num_inputs;
    bool used_textures[2];
    uint8_t num_floats;
    GLint attrib_locations[7];
    uint8_t attrib_sizes[7];
    uint8_t num_attribs;
    bool used_noise;
    GLint frame_count_location;
    GLint window_height_location;
};

static struct ShaderProgram shader_program_pool[64];
static uint8_t shader_program_pool_size;
static GLuint opengl_vbo;

static uint32_t frame_count;
static uint32_t current_height;

struct System {
    struct Display {
        int32_t width;
        int32_t height;
    } display;
    struct FBO {
        struct shader {
            GLuint program;
            GLuint vertex;
            GLuint fragment;
            struct uniforms {
                GLuint POST_POS     ;
                GLuint POST_TEXCOORD;
                GLuint POST_TEXTURE ;
                GLuint POST_DEPTH   ;
            } uniform;
        } shader;
        GLuint framebuffer;
        GLuint color_texture;
        GLuint depth_texture;
    } post;
    GLuint main_program;
    struct ShaderProgram *curShader;
} sys;

static bool gfx_opengl_z_is_from_0_to_1(void) {
    return false;
}

static void gfx_opengl_vertex_array_set_attribs(struct ShaderProgram *prg) {
    size_t num_floats = prg->num_floats;
    size_t pos = 0;

    for (int i = 0; i < prg->num_attribs; i++) {
        glEnableVertexAttribArray(prg->attrib_locations[i]);
        glVertexAttribPointer(prg->attrib_locations[i], prg->attrib_sizes[i], GL_FLOAT, GL_FALSE, num_floats * sizeof(float), (void *) (pos * sizeof(float)));
        pos += prg->attrib_sizes[i];
    }
}

static void gfx_opengl_set_uniforms(struct ShaderProgram *prg) {
    if (prg->used_noise) {
        glUniform1i(prg->frame_count_location, frame_count);
        glUniform1i(prg->window_height_location, current_height);
    }
}

static void gfx_opengl_load_shader_arrays(struct ShaderProgram *new_prg) {
    if (new_prg != NULL) {
        for (int i = 0; i < new_prg->num_attribs; i++) {
            glDisableVertexAttribArray(new_prg->attrib_locations[i]);
        }
    }
}

static void gfx_opengl_unload_shader(struct ShaderProgram *old_prg) {
    if (old_prg != NULL) {
        for (int i = 0; i < old_prg->num_attribs; i++) {
            glDisableVertexAttribArray(old_prg->attrib_locations[i]);
        }
    }
}

static void gfx_opengl_load_shader(struct ShaderProgram *new_prg) {
    sys.main_program = new_prg->opengl_program_id;
    glUseProgram(new_prg->opengl_program_id);
    gfx_opengl_vertex_array_set_attribs(new_prg);
    gfx_opengl_set_uniforms(new_prg);
    sys.curShader = new_prg;
}

static void append_str(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
}

static void append_line(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
    buf[(*len)++] = '\n';
}

static const char *shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha, bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:
                return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_INPUT_1:
                return with_alpha || !inputs_have_alpha ? "vInput1" : "vInput1.rgb";
            case SHADER_INPUT_2:
                return with_alpha || !inputs_have_alpha ? "vInput2" : "vInput2.rgb";
            case SHADER_INPUT_3:
                return with_alpha || !inputs_have_alpha ? "vInput3" : "vInput3.rgb";
            case SHADER_INPUT_4:
                return with_alpha || !inputs_have_alpha ? "vInput4" : "vInput4.rgb";
            case SHADER_TEXEL0:
                return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A:
                return hint_single_element ? "texVal0.a" :
                    (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)" : "vec3(texVal0.a, texVal0.a, texVal0.a)");
            case SHADER_TEXEL1:
                return with_alpha ? "texVal1" : "texVal1.rgb";
        }
    } else {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_INPUT_1:
                return "vInput1.a";
            case SHADER_INPUT_2:
                return "vInput2.a";
            case SHADER_INPUT_3:
                return "vInput3.a";
            case SHADER_INPUT_4:
                return "vInput4.a";
            case SHADER_TEXEL0:
                return "texVal0.a";
            case SHADER_TEXEL0A:
                return "texVal0.a";
            case SHADER_TEXEL1:
                return "texVal1.a";
        }
    }
}

static void append_formula(char *buf, size_t *len, uint8_t c[2][4], bool do_single, bool do_multiply, bool do_mix, bool with_alpha, bool only_alpha, bool opt_alpha) {
    if (do_single) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    } else if (do_multiply) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
    } else if (do_mix) {
        append_str(buf, len, "mix(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, ")");
    } else {
        append_str(buf, len, "(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " - ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ") * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, " + ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    }
}

static struct ShaderProgram *gfx_opengl_create_and_load_new_shader(uint32_t shader_id) {
    struct CCFeatures cc_features;
    gfx_cc_get_features(shader_id, &cc_features);

    char vs_buf[1024];
    char fs_buf[1024];
    size_t vs_len = 0;
    size_t fs_len = 0;
    size_t num_floats = 4;

    // Vertex shader
    append_line(vs_buf, &vs_len, "#version 110");
    append_line(vs_buf, &vs_len, "attribute vec4 aVtxPos;");
    if (cc_features.used_textures[0] || cc_features.used_textures[1]) {
        append_line(vs_buf, &vs_len, "attribute vec2 aTexCoord;");
        append_line(vs_buf, &vs_len, "varying vec2 vTexCoord;");
        num_floats += 2;
    }
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "attribute vec4 aFog;");
        append_line(vs_buf, &vs_len, "varying vec4 vFog;");
        num_floats += 4;
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "attribute vec%d aInput%d;\n", cc_features.opt_alpha ? 4 : 3, i + 1);
        vs_len += sprintf(vs_buf + vs_len, "varying vec%d vInput%d;\n", cc_features.opt_alpha ? 4 : 3, i + 1);
        num_floats += cc_features.opt_alpha ? 4 : 3;
    }
    append_line(vs_buf, &vs_len, "void main() {");
    if (cc_features.used_textures[0] || cc_features.used_textures[1]) {
        append_line(vs_buf, &vs_len, "vTexCoord = aTexCoord;");
    }
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "vFog = aFog;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "vInput%d = aInput%d;\n", i + 1, i + 1);
    }
    append_line(vs_buf, &vs_len, "gl_Position = aVtxPos;");
    append_line(vs_buf, &vs_len, "}");

    // Fragment shader
    append_line(fs_buf, &fs_len, "#version 110");
    //append_line(fs_buf, &fs_len, "precision mediump float;");
    if (cc_features.used_textures[0] || cc_features.used_textures[1]) {
        append_line(fs_buf, &fs_len, "varying vec2 vTexCoord;");
    }
    if (cc_features.opt_fog) {
        append_line(fs_buf, &fs_len, "varying vec4 vFog;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        fs_len += sprintf(fs_buf + fs_len, "varying vec%d vInput%d;\n", cc_features.opt_alpha ? 4 : 3, i + 1);
    }
    if (cc_features.used_textures[0]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex0;");
    }
    if (cc_features.used_textures[1]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex1;");
    }

    if (cc_features.opt_alpha && cc_features.opt_noise) {
        append_line(fs_buf, &fs_len, "uniform int frame_count;");
        append_line(fs_buf, &fs_len, "uniform int window_height;");

        append_line(fs_buf, &fs_len, "float random(in vec3 value) {");
        append_line(fs_buf, &fs_len, "    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));");
        append_line(fs_buf, &fs_len, "    return fract(sin(random) * 143758.5453);");
        append_line(fs_buf, &fs_len, "}");
    }

    append_line(fs_buf, &fs_len, "void main() {");

    if (cc_features.used_textures[0]) {
        append_line(fs_buf, &fs_len, "vec4 texVal0 = texture2D(uTex0, vTexCoord);");
    }
    if (cc_features.used_textures[1]) {
        append_line(fs_buf, &fs_len, "vec4 texVal1 = texture2D(uTex1, vTexCoord);");
    }

    append_str(fs_buf, &fs_len, cc_features.opt_alpha ? "vec4 texel = " : "vec3 texel = ");
    if (!cc_features.color_alpha_same && cc_features.opt_alpha) {
        append_str(fs_buf, &fs_len, "vec4(");
        append_formula(fs_buf, &fs_len, cc_features.c, cc_features.do_single[0], cc_features.do_multiply[0], cc_features.do_mix[0], false, false, true);
        append_str(fs_buf, &fs_len, ", ");
        append_formula(fs_buf, &fs_len, cc_features.c, cc_features.do_single[1], cc_features.do_multiply[1], cc_features.do_mix[1], true, true, true);
        append_str(fs_buf, &fs_len, ")");
    } else {
        append_formula(fs_buf, &fs_len, cc_features.c, cc_features.do_single[0], cc_features.do_multiply[0], cc_features.do_mix[0], cc_features.opt_alpha, false, cc_features.opt_alpha);
    }
    append_line(fs_buf, &fs_len, ";");

    if (cc_features.opt_texture_edge && cc_features.opt_alpha) {
        append_line(fs_buf, &fs_len, "if (texel.a > 0.3) texel.a = 1.0; else discard;");
    }
    // TODO discard if alpha is 0?
    if (cc_features.opt_fog) {
        if (cc_features.opt_alpha) {
            append_line(fs_buf, &fs_len, "texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);");
        } else {
            append_line(fs_buf, &fs_len, "texel = mix(texel, vFog.rgb, vFog.a);");
        }
    }

    if (cc_features.opt_alpha && cc_features.opt_noise) {
        append_line(fs_buf, &fs_len, "texel.a *= floor(random(vec3(floor(gl_FragCoord.xy * (240.0 / float(window_height))), float(frame_count))) + 0.5);");
    }

    if (cc_features.opt_alpha) {
        append_line(fs_buf, &fs_len, "gl_FragColor = texel;");
    } else {
        append_line(fs_buf, &fs_len, "gl_FragColor = vec4(texel, 1.0);");
    }
    append_line(fs_buf, &fs_len, "}");

    vs_buf[vs_len] = '\0';
    fs_buf[fs_len] = '\0';

    /*puts("Vertex shader:");
    puts(vs_buf);
    puts("Fragment shader:");
    puts(fs_buf);
    puts("End");*/

    const GLchar *sources[2] = { vs_buf, fs_buf };
    const GLint lengths[2] = { vs_len, fs_len };
    GLint success;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 0;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        fprintf(stderr, "Vertex shader compilation failed\n");
        glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
        fprintf(stderr, "%s\n", &error_log[0]);
        abort();
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 0;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        fprintf(stderr, "Fragment shader compilation failed\n");
        glGetShaderInfoLog(fragment_shader, max_length, &max_length, &error_log[0]);
        fprintf(stderr, "%s\n", &error_log[0]);
        abort();
    }

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    size_t cnt = 0;

    struct ShaderProgram *prg = &shader_program_pool[shader_program_pool_size++];
    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aVtxPos");
    prg->attrib_sizes[cnt] = 4;
    ++cnt;

    if (cc_features.used_textures[0] || cc_features.used_textures[1]) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aTexCoord");
        prg->attrib_sizes[cnt] = 2;
        ++cnt;
    }

    if (cc_features.opt_fog) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aFog");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    for (int i = 0; i < cc_features.num_inputs; i++) {
        char name[16];
        sprintf(name, "aInput%d", i + 1);
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
        prg->attrib_sizes[cnt] = cc_features.opt_alpha ? 4 : 3;
        ++cnt;
    }

    prg->shader_id = shader_id;
    prg->opengl_program_id = shader_program;
    prg->num_inputs = cc_features.num_inputs;
    prg->used_textures[0] = cc_features.used_textures[0];
    prg->used_textures[1] = cc_features.used_textures[1];
    prg->num_floats = num_floats;
    prg->num_attribs = cnt;

    gfx_opengl_load_shader(prg);

    if (cc_features.used_textures[0]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex0");
        glUniform1i(sampler_location, 0);
    }
    if (cc_features.used_textures[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex1");
        glUniform1i(sampler_location, 1);
    }

    if (cc_features.opt_alpha && cc_features.opt_noise) {
        prg->frame_count_location = glGetUniformLocation(shader_program, "frame_count");
        prg->window_height_location = glGetUniformLocation(shader_program, "window_height");
        prg->used_noise = true;
    } else {
        prg->used_noise = false;
    }

    return prg;
}

static struct ShaderProgram *gfx_opengl_lookup_shader(uint32_t shader_id) {
    for (size_t i = 0; i < shader_program_pool_size; i++) {
        if (shader_program_pool[i].shader_id == shader_id) {
            return &shader_program_pool[i];
        }
    }
    return NULL;
}

static void gfx_opengl_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static GLuint gfx_opengl_new_texture(void) {
    GLuint ret;
    glGenTextures(1, &ret);
    return ret;
}

static void gfx_opengl_select_texture(int tile, GLuint texture_id) {
    glActiveTexture(GL_TEXTURE0 + tile);
    glBindTexture(GL_TEXTURE_2D, texture_id);
}

static void gfx_opengl_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
}

static uint32_t gfx_cm_to_opengl(uint32_t val) {
    if (val & G_TX_CLAMP) {
        return GL_CLAMP_TO_EDGE;
    }
    return (val & G_TX_MIRROR) ? GL_MIRRORED_REPEAT : GL_REPEAT;
}

static void gfx_opengl_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    glActiveTexture(GL_TEXTURE0 + tile);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear_filter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear_filter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
}

static void gfx_opengl_set_depth_test(bool depth_test) {
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

static void gfx_opengl_set_depth_mask(bool z_upd) {
    glDepthMask(z_upd ? GL_TRUE : GL_FALSE);
}

static void gfx_opengl_set_zmode_decal(bool zmode_decal) {
    if (zmode_decal) {
        glPolygonOffset(-2, -2);
        glEnable(GL_POLYGON_OFFSET_FILL);
    } else {
        glPolygonOffset(0, 0);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

static void gfx_opengl_set_viewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
    current_height = height;
}

static void gfx_opengl_set_scissor(int x, int y, int width, int height) {
    glScissor(x, y, width, height);
}

static void gfx_opengl_set_use_alpha(bool use_alpha) {
    if (use_alpha) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
}

static void gfx_opengl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    //printf("flushing %d tris\n", buf_vbo_num_tris);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 3 * buf_vbo_num_tris);
}

static void throw_shader_error(GLenum shader_type, GLuint handle,
        char* shader_text) {
    GLint status;
    glGetShaderiv(handle, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[1000] = {0};
        GLsizei len;
        glGetShaderInfoLog(handle, 1000, &len, log);

        bool unknown_type = false;
        int file, line, char_num;
        size_t matched_elements = sscanf(log, "%d:%d(%d)",
                &file, &line, &char_num);
        if(matched_elements < 3) {
            size_t matched_elements = sscanf(log, "%d(%d)",
                    &line, &char_num);
            if(matched_elements < 2)
                unknown_type = true;
        }

        if(unknown_type) {
            printf("Error: Compiling failed\n%*s\n",
                    len, log);
        } else {
            char *source_line;
            char *string_ptr = shader_text;
            for(int i=0; i < line-1; i++) {
                string_ptr = strchr(string_ptr, '\n');
                string_ptr++;
            }
            source_line = strdup(string_ptr);
            *strchr(source_line, '\n') = '\0';

            printf("Error: Compiling failed\n%.*sCode:\n% 4d: %.*s\n",
                    len, log, line, 256, source_line);

            free(source_line);
        }
    }
}

static void compile_post_shaders(void) {
    char *vertex_shader_source =
        "#version 100\n"
        "attribute vec4 v_texCoord;\n"
        "attribute vec2 m_texCoord;\n"
        "varying vec2 texCoord;\n"
        "void main() {\n"
        "   gl_Position = v_texCoord;\n"
        "   texCoord = m_texCoord;\n"
        "}\n\0";

    FILE *fragment_shader = fopen("fragment.glsl", "ro");
    if(!fragment_shader) {
        fprintf(stderr, "Failed to open fragment shader\n");
        exit(EXIT_FAILURE);
    }

    fseek(fragment_shader, 0, SEEK_END);
    size_t frag_size = ftell(fragment_shader);
    fseek(fragment_shader, 0, SEEK_SET);

    char *fragment_shader_source = malloc(frag_size+4096);
    memset(fragment_shader_source, 0x00, frag_size+4096);
    fread(fragment_shader_source, 1, frag_size-1, fragment_shader);
    fragment_shader_source[frag_size] = '\0';

    sys.post.shader.vertex = glCreateShader(GL_VERTEX_SHADER);
    sys.post.shader.fragment = glCreateShader(GL_FRAGMENT_SHADER);
    
    glShaderSource(sys.post.shader.vertex, 1, 
            &vertex_shader_source, NULL);
    glShaderSource(sys.post.shader.fragment, 1, 
            &fragment_shader_source, NULL);
    
    glCompileShader(sys.post.shader.vertex);
    glCompileShader(sys.post.shader.fragment);
    
    throw_shader_error(GL_VERTEX_SHADER, sys.post.shader.vertex,
            vertex_shader_source);
    throw_shader_error(GL_FRAGMENT_SHADER, sys.post.shader.fragment,
            fragment_shader_source);
    
    sys.post.shader.program = glCreateProgram();
    glAttachShader(sys.post.shader.program, sys.post.shader.vertex);
    glAttachShader(sys.post.shader.program, sys.post.shader.fragment);

    glLinkProgram(sys.post.shader.program);

    glDeleteShader(sys.post.shader.vertex);
    glDeleteShader(sys.post.shader.fragment);
    fclose(fragment_shader);
}

static void create_post_textures(void) {
    GLint old_program;
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);

    // switch to appropriate shader
    glUseProgram(sys.post.shader.program);

    // set texture properties
    glGenTextures(1,               &sys.post.color_texture);
    glBindTexture(GL_TEXTURE_2D,   sys.post.color_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 
            sys.display.width,
            sys.display.height,
            0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    // set texture properties
    glGenTextures(1,             &sys.post.depth_texture);
    glBindTexture(GL_TEXTURE_2D,  sys.post.depth_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
            sys.display.width,
            sys.display.height,
            0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
    // set framebuffer properties
    glGenFramebuffers(1,             &sys.post.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, sys.post.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, sys.post.color_texture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D, sys.post.depth_texture, 0);
    
    // check if it borked
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status != GL_FRAMEBUFFER_COMPLETE) {
        printf("Failed to create framebuffer(s)! Error code 0x%X\n",
                status);
    }   
    sys.post.shader.uniform.POST_POS        = glGetAttribLocation(sys.post.shader.program,  "v_texCoord");
    sys.post.shader.uniform.POST_TEXCOORD   = glGetAttribLocation(sys.post.shader.program,  "m_texCoord");
    sys.post.shader.uniform.POST_TEXTURE    = glGetUniformLocation(sys.post.shader.program, "color_texture");
    sys.post.shader.uniform.POST_DEPTH      = glGetUniformLocation(sys.post.shader.program, "depth_texture");
    
    glUseProgram(old_program);
}

static void gfx_opengl_init(void) {
#if FOR_WINDOWS
    glewInit();
#endif
    
    compile_post_shaders();
    create_post_textures();
    
    glGenBuffers(1, &opengl_vbo);
    
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
    
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void gfx_opengl_on_resize(void) {
}

static void update_texture_size(void) {
    GLint old_program;
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
    
    // switch to appropriate shader
    glUseProgram(sys.post.shader.program);
    
    glBindTexture(GL_TEXTURE_2D,   sys.post.color_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 
            sys.display.width,
            sys.display.height,
            0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

    glBindTexture(GL_TEXTURE_2D,  sys.post.depth_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
            sys.display.width,
            sys.display.height,
            0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    
    glBindFramebuffer(GL_FRAMEBUFFER, sys.post.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, sys.post.color_texture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D, sys.post.depth_texture, 0);

    glUseProgram(old_program);
}

static void update_display_size(void) {
    GLint m_viewport[4];
    bool resized = false; 
    glGetIntegerv( GL_VIEWPORT, m_viewport );

    if(m_viewport[2] != sys.display.width ||
            m_viewport[3] != sys.display.height) {
        sys.display.width = m_viewport[2];
        sys.display.height = m_viewport[3];
        printf("Display resized to: %d %d\n",
                sys.display.width,
                sys.display.height);
        resized = true;
    }

    if(!resized)
        return;

    // if it was resized then change the texture size
    update_texture_size();
}

static void gfx_opengl_start_frame(void) {
    frame_count++;

    update_display_size();

    glBindFramebuffer(GL_FRAMEBUFFER, sys.post.framebuffer);

    glUseProgram(sys.main_program);
    gfx_opengl_load_shader_arrays(sys.curShader);
    
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);

    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE); // Must be set to clear Z-buffer
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
}

//struct System {
//    struct Display {
//        uint32_t width;
//        uint32_t height;
//    } display;
//    struct FBO {
//        struct shader {
//            GLuint program;
//            GLuint vertex;
//            GLuint fragment;
//            struct uniforms {
//                GLuint POST_POS     ;
//                GLuint POST_TEXCOORD;
//                GLuint POST_TEXTURE ;
//                GLuint POST_DEPTH   ;
//            } uniform;
//        } shader;
//        GLuint framebuffer;
//        GLuint color_texture;
//        GLuint depth_texture;
//    } post;
//    GLuint main_program;
//} sys;

static void gfx_opengl_end_frame(void) {
    gfx_opengl_unload_shader(sys.curShader);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glUseProgram(sys.post.shader.program);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    GLfloat postVertices[] = {
        -1.0f, -1.0f,  // Position 0
         0.0f,  0.0f,  // TexCoord 0
         1.0f, -1.0f,  // Position 1
         1.0f,  0.0f,  // TexCoord 1
         1.0f,  1.0f,  // Position 2
         1.0f,  1.0f,  // TexCoord 2
        -1.0f,  1.0f,  // Position 3
         0.0f,  1.0f}; // TexCoord 3

    glVertexAttribPointer(sys.post.shader.uniform.POST_POS,
        2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), postVertices);
    glVertexAttribPointer(sys.post.shader.uniform.POST_TEXCOORD,
        2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), &postVertices[2]);
    
    // enable the use of the following attribute elements
    glEnableVertexAttribArray(sys.post.shader.uniform.POST_POS);
    glEnableVertexAttribArray(sys.post.shader.uniform.POST_TEXCOORD);
    
    // Bind the textures
    // Render texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sys.post.color_texture);
    glUniform1i(sys.post.shader.uniform.POST_TEXTURE, 0);
    
    // Depth texture
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sys.post.depth_texture);
    glUniform1i(sys.post.shader.uniform.POST_DEPTH, 1);
    
    // draw frame
    GLushort indices[] = { 0, 1, 2, 0, 2, 3 };
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    
    glDisableVertexAttribArray(sys.post.shader.uniform.POST_POS);
    glDisableVertexAttribArray(sys.post.shader.uniform.POST_TEXCOORD);
}

static void gfx_opengl_finish_render(void) {
}

struct GfxRenderingAPI gfx_opengl_api = {
    gfx_opengl_z_is_from_0_to_1,
    gfx_opengl_unload_shader,
    gfx_opengl_load_shader,
    gfx_opengl_create_and_load_new_shader,
    gfx_opengl_lookup_shader,
    gfx_opengl_shader_get_info,
    gfx_opengl_new_texture,
    gfx_opengl_select_texture,
    gfx_opengl_upload_texture,
    gfx_opengl_set_sampler_parameters,
    gfx_opengl_set_depth_test,
    gfx_opengl_set_depth_mask,
    gfx_opengl_set_zmode_decal,
    gfx_opengl_set_viewport,
    gfx_opengl_set_scissor,
    gfx_opengl_set_use_alpha,
    gfx_opengl_draw_triangles,
    gfx_opengl_init,
    gfx_opengl_on_resize,
    gfx_opengl_start_frame,
    gfx_opengl_end_frame,
    gfx_opengl_finish_render
};

#endif
