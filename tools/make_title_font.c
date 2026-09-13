/* Builds title_font.h: signed distance fields for the title's glyphs.
 *
 * A distance field keeps edges sharp at any scale, so one small atlas draws
 * the title across any screen, with an outline of any width:
 *
 *   byte  255 ...... 128 | 127 ...... 0
 *         deep inside   edge   far outside
 *
 * Each glyph is rendered SUPERSAMPLE times larger, its exact distances taken
 * there (Felzenszwalb's transform), then sampled once per field texel.
 *
 * Usage: make_title_font FONT.ttf OUTPUT.h
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

static const char TEXT[] = "BALLOON TASKS!";

enum {
    SUPERSAMPLE = 8,  /* rendered pixels per field texel */
    EM = 96,          /* field texels per em */
    SPREAD = 12,      /* field texels from the edge to byte 0 or 255 */
    WEIGHT = 700,     /* Fredoka Bold */
    ATLAS_GAP = 2,
    MAX_GLYPHS = 16,
    MAX_AXES = 16,
};

static const double FAR = 1e20;

typedef struct {
    char c;
    float advance;
    float left, top;           /* field box from the pen; top is up from the baseline */
    int width, height;
    int atlas_x;
    float ink_left, ink_right;
    float cx, cy;              /* ink centroid from the pen, y up */
    float area;
    unsigned char *field;
} Glyph;

/* Squared distance from each sample to the nearest zero of f, in one line. */
static void edt_line(const double *f, double *d, int *v, double *z, int n)
{
    int k = 0;
    v[0] = 0;
    z[0] = -FAR;
    z[1] = FAR;
    for (int q = 1; q < n; q++) {
        double s;
        for (;;) {
            s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k])) / (2.0 * q - 2.0 * v[k]);
            if (s > z[k]) {
                break;
            }
            k--;
        }
        k++;
        v[k] = q;
        z[k] = s;
        z[k + 1] = FAR;
    }

    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[k + 1] < q) {
            k++;
        }
        d[q] = (double)(q - v[k]) * (q - v[k]) + f[v[k]];
    }
}

/* Squared distance from each pixel to the nearest pixel whose mask is target. */
static double *edt(const unsigned char *mask, int width, int height, unsigned char target)
{
    int n = width > height ? width : height;
    double *grid = malloc(sizeof(double) * width * height);
    double *f = malloc(sizeof(double) * n);
    double *d = malloc(sizeof(double) * n);
    double *z = malloc(sizeof(double) * (n + 1));
    int *v = malloc(sizeof(int) * n);
    if (!grid || !f || !d || !z || !v) {
        fprintf(stderr, "make_title_font: out of memory\n");
        exit(1);
    }

    for (int i = 0; i < width * height; i++) {
        grid[i] = mask[i] == target ? 0.0 : FAR;
    }

    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            f[y] = grid[y * width + x];
        }
        edt_line(f, d, v, z, height);
        for (int y = 0; y < height; y++) {
            grid[y * width + x] = d[y];
        }
    }

    for (int y = 0; y < height; y++) {
        memcpy(f, grid + y * width, sizeof(double) * width);
        edt_line(f, d, v, z, width);
        memcpy(grid + y * width, d, sizeof(double) * width);
    }

    free(f);
    free(d);
    free(z);
    free(v);
    return grid;
}

static void set_weight(FT_Library library, FT_Face face)
{
    FT_MM_Var *mm;
    if (FT_Get_MM_Var(face, &mm)) {
        return;
    }

    FT_Fixed coords[MAX_AXES];
    FT_UInt axes = mm->num_axis < MAX_AXES ? mm->num_axis : MAX_AXES;
    for (FT_UInt a = 0; a < axes; a++) {
        bool weight = mm->axis[a].tag == FT_MAKE_TAG('w', 'g', 'h', 't');
        coords[a] = weight ? (FT_Fixed)WEIGHT << 16 : mm->axis[a].def;
    }
    FT_Set_Var_Design_Coordinates(face, axes, coords);
    FT_Done_MM_Var(library, mm);
}

static bool build_glyph(FT_Face face, char c, Glyph *g)
{
    memset(g, 0, sizeof(*g));
    g->c = c;
    if (FT_Load_Char(face, (unsigned char)c, FT_LOAD_RENDER | FT_LOAD_NO_HINTING)) {
        return false;
    }

    FT_GlyphSlot slot = face->glyph;
    const FT_Bitmap *bitmap = &slot->bitmap;
    g->advance = (float)slot->advance.x / 64.0f / SUPERSAMPLE;
    int ink_width = (int)bitmap->width;
    int ink_height = (int)bitmap->rows;
    if (ink_width == 0 || ink_height == 0) {
        return true;
    }

    /* Room around the ink for the field to fall to byte 0. */
    int pad = (SPREAD + 1) * SUPERSAMPLE;
    g->width = (ink_width + 2 * pad + SUPERSAMPLE - 1) / SUPERSAMPLE;
    g->height = (ink_height + 2 * pad + SUPERSAMPLE - 1) / SUPERSAMPLE;
    int big_width = g->width * SUPERSAMPLE;
    int big_height = g->height * SUPERSAMPLE;
    unsigned char *mask = calloc((size_t)big_width * big_height, 1);
    g->field = malloc((size_t)g->width * g->height);
    if (!mask || !g->field) {
        return false;
    }

    double area = 0.0, sum_x = 0.0, sum_y = 0.0;
    int pitch = bitmap->pitch < 0 ? -bitmap->pitch : bitmap->pitch;
    for (int row = 0; row < ink_height; row++) {
        for (int col = 0; col < ink_width; col++) {
            unsigned char coverage = bitmap->buffer[row * pitch + col];
            mask[(row + pad) * big_width + col + pad] = coverage >= 128;
            area += coverage;
            sum_x += coverage * (slot->bitmap_left + col + 0.5);
            sum_y += coverage * (slot->bitmap_top - row - 0.5);
        }
    }

    double *to_outside = edt(mask, big_width, big_height, 0);
    double *to_inside = edt(mask, big_width, big_height, 1);
    for (int y = 0; y < g->height; y++) {
        for (int x = 0; x < g->width; x++) {
            int p = (y * SUPERSAMPLE + SUPERSAMPLE / 2) * big_width + x * SUPERSAMPLE + SUPERSAMPLE / 2;
            double distance = mask[p] ? sqrt(to_outside[p]) - 0.5 : 0.5 - sqrt(to_inside[p]);
            double value = 127.5 + distance / SUPERSAMPLE * 127.0 / SPREAD;
            g->field[y * g->width + x] = (unsigned char)(value < 0 ? 0 : value > 255 ? 255 : value + 0.5);
        }
    }

    g->left = (float)(slot->bitmap_left - pad) / SUPERSAMPLE;
    g->top = (float)(slot->bitmap_top + pad) / SUPERSAMPLE;
    g->ink_left = (float)slot->bitmap_left / SUPERSAMPLE;
    g->ink_right = (float)(slot->bitmap_left + ink_width) / SUPERSAMPLE;
    g->cx = (float)(sum_x / area / SUPERSAMPLE);
    g->cy = (float)(sum_y / area / SUPERSAMPLE);
    g->area = (float)(area / 255.0 / SUPERSAMPLE / SUPERSAMPLE);

    free(to_outside);
    free(to_inside);
    free(mask);
    return true;
}

static void write_header(FILE *out, Glyph *glyphs, int count, int atlas_width, int atlas_height)
{
    fprintf(out, "/* Generated by tools/make_title_font.c from Fredoka Bold. */\n");
    fprintf(out, "#ifndef BALLOON_TASKS_TITLE_FONT_H\n#define BALLOON_TASKS_TITLE_FONT_H\n\n");
    fprintf(out, "#include <stdint.h>\n\n");
    fprintf(out, "#define TITLE_TEXT \"%s\"\n", TEXT);
    fprintf(out, "#define TITLE_EM %d      /* field texels per em */\n", EM);
    fprintf(out, "#define TITLE_SPREAD %d  /* field texels from the edge to byte 0 or 255 */\n", SPREAD);
    fprintf(out, "#define TITLE_GLYPHS %d\n", count);
    fprintf(out, "#define TITLE_ATLAS_WIDTH %d\n", atlas_width);
    fprintf(out, "#define TITLE_ATLAS_HEIGHT %d\n\n", atlas_height);
    fprintf(out, "/* Lengths in field texels, from the pen on the baseline, y up. */\n"
            "typedef struct {\n"
            "    char c;\n"
            "    float advance;\n"
            "    float left, top;\n"
            "    int width, height;\n"
            "    int atlas_x;\n"
            "    float ink_left, ink_right;\n"
            "    float cx, cy;\n"
            "    float area;\n"
            "} TitleGlyph;\n\n");
    fprintf(out, "static const TitleGlyph title_glyphs[TITLE_GLYPHS] = {\n");
    for (int i = 0; i < count; i++) {
        const Glyph *g = &glyphs[i];
        fprintf(out, "    {'%c', %.4ff, %.4ff, %.4ff, %d, %d, %d, %.4ff, %.4ff, %.4ff, %.4ff, %.4ff},\n",
                g->c, g->advance, g->left, g->top, g->width, g->height, g->atlas_x,
                g->ink_left, g->ink_right, g->cx, g->cy, g->area);
    }
    fprintf(out, "};\n\n");

    fprintf(out, "static const uint8_t title_atlas[TITLE_ATLAS_WIDTH * TITLE_ATLAS_HEIGHT] = {\n");
    for (int y = 0; y < atlas_height; y++) {
        fprintf(out, "   ");
        for (int x = 0; x < atlas_width; x++) {
            unsigned char value = 0;
            for (int i = 0; i < count; i++) {
                const Glyph *g = &glyphs[i];
                if (g->field && x >= g->atlas_x && x < g->atlas_x + g->width && y < g->height) {
                    value = g->field[y * g->width + x - g->atlas_x];
                }
            }
            fprintf(out, " %u,", value);
        }
        fprintf(out, "\n");
    }
    fprintf(out, "};\n\n#endif\n");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: make_title_font FONT.ttf OUTPUT.h\n");
        return 1;
    }

    FT_Library library;
    FT_Face face;
    if (FT_Init_FreeType(&library) || FT_New_Face(library, argv[1], 0, &face)) {
        fprintf(stderr, "make_title_font: cannot open %s\n", argv[1]);
        return 1;
    }
    set_weight(library, face);
    FT_Set_Pixel_Sizes(face, 0, EM * SUPERSAMPLE);

    /* One glyph per distinct character, side by side in the atlas. */
    Glyph glyphs[MAX_GLYPHS];
    int count = 0, atlas_width = 0, atlas_height = 1;
    for (const char *c = TEXT; *c; c++) {
        if (memchr(TEXT, *c, (size_t)(c - TEXT))) {
            continue;
        }
        Glyph *g = &glyphs[count++];
        if (!build_glyph(face, *c, g)) {
            fprintf(stderr, "make_title_font: cannot render '%c'\n", *c);
            return 1;
        }
        g->atlas_x = atlas_width;
        if (g->width > 0) {
            atlas_width += g->width + ATLAS_GAP;
        }
        if (g->height > atlas_height) {
            atlas_height = g->height;
        }
    }

    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        fprintf(stderr, "make_title_font: cannot write %s\n", argv[2]);
        return 1;
    }
    write_header(out, glyphs, count, atlas_width, atlas_height);
    fclose(out);
    return 0;
}
