#include "title.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <GLES2/gl2.h>

#include "title_font.h"

enum { TITLE_LENGTH = sizeof(TITLE_TEXT) - 1 };

/* Timeline, in seconds. */
static const double FADE_IN = 0.5;
static const double HOLD = 2.0;
static const double FADE_OUT = 0.5;

/* Share of the screen width the ink spans. */
static const float WIDTH_SHARE = 0.75f;

/* Radius of the arc's circle, in screen heights. */
static const float ARC_RADIUS = 4.5f;

/* Black outline around the white letters, in ems. */
static const float OUTLINE_EM = 0.07f;

enum TitleLayer {
    LAYER_OUTLINE,
    LAYER_FILL,
};

struct Title {
    GLuint program;
    GLuint texture;
    GLint color_loc;
    GLint edge_loc;
    GLint ramp_loc;
};

/* A glyph's place on screen: its pen centre on the baseline, and the
   direction of its baseline. */
typedef struct {
    const TitleGlyph *glyph;
    float x, y;
    float cos, sin;
} Pose;

static const char *vert_src =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

/* Covers what lies within u_edge texels of the glyph's edge, blending over
   u_ramp texels (one screen pixel) for antialiasing. */
static const char *frag_src =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_color;\n"
    "uniform float u_edge;\n"
    "uniform float u_ramp;\n"
    "uniform float u_spread;\n"
    "void main() {\n"
    "    float d = (texture2D(u_tex, v_uv).r * 255.0 - 127.5) * u_spread / 127.0;\n"
    "    gl_FragColor = u_color * clamp((d - u_edge) / u_ramp + 0.5, 0.0, 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "balloon-tasks: title shader: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(void)
{
    GLuint vert = compile(GL_VERTEX_SHADER, vert_src);
    GLuint frag = compile(GL_FRAGMENT_SHADER, frag_src);
    if (!vert || !frag) {
        glDeleteShader(vert);
        glDeleteShader(frag);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glBindAttribLocation(program, 0, "a_pos");
    glBindAttribLocation(program, 1, "a_uv");
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

Title *title_create(void)
{
    Title *title = calloc(1, sizeof(*title));
    if (!title) {
        return NULL;
    }

    title->program = link_program();
    if (!title->program) {
        free(title);
        return NULL;
    }

    /* Sampler and spread never change; set them while the program is current. */
    GLint previous = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
    glUseProgram(title->program);
    glUniform1i(glGetUniformLocation(title->program, "u_tex"), 0);
    glUniform1f(glGetUniformLocation(title->program, "u_spread"), (float)TITLE_SPREAD);
    title->color_loc = glGetUniformLocation(title->program, "u_color");
    title->edge_loc = glGetUniformLocation(title->program, "u_edge");
    title->ramp_loc = glGetUniformLocation(title->program, "u_ramp");
    glUseProgram((GLuint)previous);

    glGenTextures(1, &title->texture);
    glBindTexture(GL_TEXTURE_2D, title->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, TITLE_ATLAS_WIDTH, TITLE_ATLAS_HEIGHT, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, title_atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return title;
}

void title_destroy(Title *title)
{
    if (!title) {
        return;
    }
    glDeleteTextures(1, &title->texture);
    glDeleteProgram(title->program);
    free(title);
}

bool title_playing(double elapsed)
{
    return elapsed < FADE_IN + HOLD + FADE_OUT;
}

static float title_alpha(double elapsed)
{
    if (elapsed < FADE_IN) {
        return (float)(elapsed / FADE_IN);
    }
    if (elapsed < FADE_IN + HOLD) {
        return 1.0f;
    }
    if (!title_playing(elapsed)) {
        return 0.0f;
    }
    return (float)((FADE_IN + HOLD + FADE_OUT - elapsed) / FADE_OUT);
}

static const TitleGlyph *title_glyph(char c)
{
    for (int i = 0; i < TITLE_GLYPHS; i++) {
        if (title_glyphs[i].c == c) {
            return &title_glyphs[i];
        }
    }
    return &title_glyphs[0];
}

/* Lays the text along a circle far bigger than the screen, its top at the
 * screen centre, each glyph turned to follow it:
 *
 *              B A L L O O N   T A S K S !
 *          .-'                             '-.
 *     .-'                                       '-.
 *                         |
 *                  centre, 4.5 screens below
 *
 * then shifts it all so the ink's centre of mass is at the screen centre.
 * Returns screen pixels per field texel. */
static float title_layout(int width, int height, Pose poses[TITLE_LENGTH])
{
    float pens[TITLE_LENGTH];
    float pen = 0.0f, ink_left = 0.0f, ink_right = 0.0f;
    for (int i = 0; i < TITLE_LENGTH; i++) {
        const TitleGlyph *g = title_glyph(TITLE_TEXT[i]);
        poses[i].glyph = g;
        pens[i] = pen;
        if (i == 0) {
            ink_left = pen + g->ink_left;
        }
        if (g->width > 0) {
            ink_right = pen + g->ink_right;
        }
        pen += g->advance;
    }

    float scale = WIDTH_SHARE * (float)width / (ink_right - ink_left);
    float radius = ARC_RADIUS * (float)height;
    float middle = (ink_left + ink_right) * 0.5f;
    float mass = 0.0f, mass_x = 0.0f, mass_y = 0.0f;
    for (int i = 0; i < TITLE_LENGTH; i++) {
        Pose *pose = &poses[i];
        const TitleGlyph *g = pose->glyph;
        float angle = (pens[i] + g->advance * 0.5f - middle) * scale / radius;
        pose->cos = cosf(angle);
        pose->sin = sinf(angle);
        pose->x = (float)width * 0.5f + radius * pose->sin;
        pose->y = (float)height * 0.5f + radius * (1.0f - pose->cos);

        float along = (g->cx - g->advance * 0.5f) * scale;
        float up = g->cy * scale;
        mass += g->area;
        mass_x += g->area * (pose->x + along * pose->cos + up * pose->sin);
        mass_y += g->area * (pose->y + along * pose->sin - up * pose->cos);
    }

    float shift_x = (float)width * 0.5f - mass_x / mass;
    float shift_y = (float)height * 0.5f - mass_y / mass;
    for (int i = 0; i < TITLE_LENGTH; i++) {
        poses[i].x += shift_x;
        poses[i].y += shift_y;
    }
    return scale;
}

static void draw_glyph(const Pose *pose, float scale, int width, int height)
{
    const TitleGlyph *g = pose->glyph;
    if (g->width == 0) {
        return;
    }

    /* Box corners along and up from the pen centre: TL, TR, BL, BR. */
    float left = (g->left - g->advance * 0.5f) * scale;
    float right = left + (float)g->width * scale;
    float top = g->top * scale;
    float bottom = top - (float)g->height * scale;
    const float along[4] = {left, right, left, right};
    const float up[4] = {top, top, bottom, bottom};
    float u0 = (float)g->atlas_x / TITLE_ATLAS_WIDTH;
    float u1 = (float)(g->atlas_x + g->width) / TITLE_ATLAS_WIDTH;
    float v1 = (float)g->height / TITLE_ATLAS_HEIGHT;
    const float u[4] = {u0, u1, u0, u1};
    const float v[4] = {0.0f, 0.0f, v1, v1};

    GLfloat verts[16];
    for (int k = 0; k < 4; k++) {
        float x = pose->x + along[k] * pose->cos + up[k] * pose->sin;
        float y = pose->y + along[k] * pose->sin - up[k] * pose->cos;
        verts[k * 4] = 2.0f * x / (float)width - 1.0f;
        verts[k * 4 + 1] = 1.0f - 2.0f * y / (float)height;
        verts[k * 4 + 2] = u[k];
        verts[k * 4 + 3] = v[k];
    }
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), verts);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), verts + 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* Output is premultiplied. The fill adds white without alpha: every fill
   lies on its own black outline, which already carries the alpha. So a
   half-faded title is half-transparent white on black, not grey. */
static void draw_layer(const Title *title, enum TitleLayer layer, float alpha,
                       const Pose poses[TITLE_LENGTH], float scale, int width, int height)
{
    if (layer == LAYER_OUTLINE) {
        glUniform4f(title->color_loc, 0.0f, 0.0f, 0.0f, alpha);
        glUniform1f(title->edge_loc, -OUTLINE_EM * TITLE_EM);
    } else {
        glUniform4f(title->color_loc, alpha, alpha, alpha, 0.0f);
        glUniform1f(title->edge_loc, 0.0f);
    }
    for (int i = 0; i < TITLE_LENGTH; i++) {
        draw_glyph(&poses[i], scale, width, height);
    }
}

void title_draw(const Title *title, double elapsed, int width, int height)
{
    float alpha = title_alpha(elapsed);
    if (alpha <= 0.0f || width <= 0 || height <= 0) {
        return;
    }

    Pose poses[TITLE_LENGTH];
    float scale = title_layout(width, height, poses);

    GLint previous = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
    glUseProgram(title->program);
    glBindTexture(GL_TEXTURE_2D, title->texture);
    glUniform1f(title->ramp_loc, 1.0f / scale);

    /* All outlines first, so no outline covers a neighbour's fill. */
    draw_layer(title, LAYER_OUTLINE, alpha, poses, scale, width, height);
    draw_layer(title, LAYER_FILL, alpha, poses, scale, width, height);
    glUseProgram((GLuint)previous);
}
