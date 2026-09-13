#include "task_text.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "task_font.h"

typedef struct {
    uint8_t r, g, b, a;
} Color;

/* Glyph outlines are drawn this many pixels around the fill. */
enum { TEXT_OUTLINE = 1 };

/* Text is clipped past this x; e.g. an oversize task stays inside its field. */
enum { NO_CLIP = 1 << 30 };

enum TextLayer {
    LAYER_OUTLINE,
    LAYER_FILL,
};

static void blend(TaskBitmap *bitmap, int x, int y, Color color)
{
    if (x < 0 || y < 0 || x >= bitmap->width || y >= bitmap->height || color.a == 0) {
        return;
    }

    uint8_t *dst = bitmap->pixels + ((size_t)y * bitmap->width + x) * 4;
    unsigned inv = 255 - color.a;
    dst[0] = (uint8_t)((color.r * color.a + dst[0] * inv + 127) / 255);
    dst[1] = (uint8_t)((color.g * color.a + dst[1] * inv + 127) / 255);
    dst[2] = (uint8_t)((color.b * color.a + dst[2] * inv + 127) / 255);
    dst[3] = (uint8_t)(color.a + (dst[3] * inv + 127) / 255);
}

static void fill(TaskBitmap *bitmap, int x, int y, int width, int height, Color color)
{
    for (int py = y; py < y + height; py++) {
        for (int px = x; px < x + width; px++) {
            blend(bitmap, px, py, color);
        }
    }
}

static const TaskGlyph *face_glyph(enum TaskFaceSize size, unsigned char c)
{
    if (c < TASK_FONT_FIRST || c >= TASK_FONT_FIRST + TASK_FONT_GLYPHS) {
        c = '?';
    }
    return &task_faces[size].glyphs[c - TASK_FONT_FIRST];
}

static int text_width(const char *value, size_t length, enum TaskFaceSize size)
{
    int width = 0;
    for (size_t i = 0; i < length && value[i]; i++) {
        width += face_glyph(size, (unsigned char)value[i])->advance;
    }
    return width;
}

/* Blends one glyph's coverage with its pen at (x, baseline). */
static void glyph(TaskBitmap *bitmap, const TaskGlyph *g, int x, int baseline, int clip_x,
                  Color color)
{
    for (int row = 0; row < g->h; row++) {
        for (int col = 0; col < g->w; col++) {
            int px = x + g->x + col;
            if (px >= clip_x) {
                break;
            }

            uint8_t coverage = task_font_pixels[g->offset + (size_t)row * g->w + col];
            if (!coverage) {
                continue;
            }

            Color pixel = color;
            pixel.a = (uint8_t)((color.a * coverage + 127) / 255);
            blend(bitmap, px, baseline + g->y + row, pixel);
        }
    }
}

static void text_layer(TaskBitmap *bitmap, const char *value, int x, int baseline,
                       enum TaskFaceSize size, int clip_x, enum TextLayer layer, Color color)
{
    static const int outline_offsets[][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };

    for (size_t i = 0; value[i]; i++) {
        const TaskGlyph *g = face_glyph(size, (unsigned char)value[i]);
        if (layer == LAYER_FILL) {
            glyph(bitmap, g, x, baseline, clip_x, color);
            x += g->advance;
            continue;
        }

        for (size_t k = 0; k < sizeof(outline_offsets) / sizeof(outline_offsets[0]); k++) {
            glyph(bitmap, g, x + outline_offsets[k][0] * TEXT_OUTLINE,
                  baseline + outline_offsets[k][1] * TEXT_OUTLINE, clip_x, color);
        }
        x += g->advance;
    }
}

/* Draws outlined text whose line box starts at (x, y). Outlines go down
   first so a glyph's outline never covers its neighbour's fill. */
static void text(TaskBitmap *bitmap, const char *value, int x, int y, enum TaskFaceSize size,
                 int clip_x, Color color, Color outline)
{
    int baseline = y + task_faces[size].ascent;
    text_layer(bitmap, value, x, baseline, size, clip_x, LAYER_OUTLINE, outline);
    text_layer(bitmap, value, x, baseline, size, clip_x, LAYER_FILL, color);
}

static bool bitmap_alloc(TaskBitmap *bitmap, int width, int height)
{
    memset(bitmap, 0, sizeof(*bitmap));
    bitmap->pixels = calloc((size_t)width * height, 4);
    if (!bitmap->pixels) {
        return false;
    }
    bitmap->width = width;
    bitmap->height = height;
    return true;
}

bool task_label_bitmap(const char *value, TaskBitmap *bitmap)
{
    int width = text_width(value, strlen(value), TASK_FACE_NORMAL) + 2 * TEXT_OUTLINE;
    int height = task_faces[TASK_FACE_NORMAL].line_height + 2 * TEXT_OUTLINE;
    if (!bitmap_alloc(bitmap, width, height)) {
        return false;
    }
    text(bitmap, value, TEXT_OUTLINE, TEXT_OUTLINE, TASK_FACE_NORMAL, NO_CLIP,
    (Color) {
        255, 255, 255, 255
    }, (Color) {
        4, 24, 63, 255
    });
    return true;
}

bool task_panel_bitmap(const char *value, bool active, bool has_started,
                       int tasks_left, size_t cursor, bool close_pressed,
                       TaskBitmap *bitmap)
{
    if (!bitmap_alloc(bitmap, TASK_PANEL_WIDTH, TASK_PANEL_HEIGHT)) {
        return false;
    }

    fill(bitmap, 0, 0, bitmap->width, bitmap->height, (Color) {
        7, 19, 42, 204
    });
    fill(bitmap, 0, 0, bitmap->width, 3, (Color) {
        57, 151, 255, 255
    });
    fill(bitmap, 0, bitmap->height - 3, bitmap->width, 3, (Color) {
        57, 151, 255, 255
    });
    fill(bitmap, 0, 0, 3, bitmap->height, (Color) {
        57, 151, 255, 255
    });
    fill(bitmap, bitmap->width - 3, 0, 3, bitmap->height, (Color) {
        57, 151, 255, 255
    });
    text(bitmap, "X", close_pressed ? 8 : 14, close_pressed ? 0 : 10,
         close_pressed ? TASK_FACE_LARGE : TASK_FACE_NORMAL, NO_CLIP,
    (Color) {
        255, 255, 255, 255
    }, (Color) {
        3, 12, 30, 255
    });
    const int title_y = 10;
    const char *title = "New Task";
    int title_x = (TASK_PANEL_WIDTH - text_width(title, strlen(title), TASK_FACE_NORMAL)) / 2;
    text(bitmap, title, title_x, title_y, TASK_FACE_NORMAL, NO_CLIP,
    (Color) {
        255, 255, 255, 255
    }, (Color) {
        3, 12, 30, 255
    });

    /* The hint shares the title's baseline. */
    const char *hint = "ENTER TO ADD";
    int hint_x = TASK_PANEL_WIDTH - 14 - text_width(hint, strlen(hint), TASK_FACE_SMALL);
    int hint_y = title_y + task_faces[TASK_FACE_NORMAL].ascent - task_faces[TASK_FACE_SMALL].ascent;
    text(bitmap, hint, hint_x, hint_y, TASK_FACE_SMALL, NO_CLIP,
    (Color) {
        166, 205, 255, 255
    }, (Color) {
        3, 12, 30, 255
    });

    Color border = active ? (Color) {
        255, 196, 74, 255
} : (Color) {
        89, 130, 179, 255
    };
    fill(bitmap, 14, 42, TASK_PANEL_WIDTH - 28, 44, border);
    fill(bitmap, 17, 45, TASK_PANEL_WIDTH - 34, 38, (Color) {
        245, 248, 255, 245
    });
    text(bitmap, value, TASK_INPUT_TEXT_X, 52, TASK_FACE_NORMAL,
         TASK_INPUT_TEXT_X + TASK_INPUT_TEXT_WIDTH,
    (Color) {
        10, 28, 58, 255
    }, (Color) {
        255, 255, 255, 255
    });
    if (active) {
        int cursor_x = TASK_INPUT_TEXT_X + task_text_offset(value, cursor);
        fill(bitmap, cursor_x, 51, 2, 21, (Color) {
            18, 92, 190, 255
        });
    }
    char status[48];
    if (!has_started) {
        snprintf(status, sizeof(status), "Add a TASK to get Started!");
    } else if (tasks_left == 0) {
        snprintf(status, sizeof(status), "NO Tasks left to POP!");
    } else if (tasks_left == 1) {
        snprintf(status, sizeof(status), "ONE Task left to POP!");
    } else {
        snprintf(status, sizeof(status), "%d Tasks left to POP!", tasks_left);
    }
    int status_x = (TASK_PANEL_WIDTH - text_width(status, strlen(status), TASK_FACE_NORMAL)) / 2;
    text(bitmap, status, status_x, 104, TASK_FACE_NORMAL, NO_CLIP,
    (Color) {
        166, 205, 255, 255
    }, (Color) {
        3, 12, 30, 255
    });
    return true;
}

void task_bitmap_free(TaskBitmap *bitmap)
{
    free(bitmap->pixels);
    memset(bitmap, 0, sizeof(*bitmap));
}

int task_text_offset(const char *text, size_t cursor)
{
    return text_width(text, cursor, TASK_FACE_NORMAL);
}

size_t task_text_cursor(const char *text, int offset)
{
    int pen = 0;
    size_t i = 0;
    for (; text[i]; i++) {
        int advance = face_glyph(TASK_FACE_NORMAL, (unsigned char)text[i])->advance;
        if (offset < pen + advance / 2) {
            return i;
        }
        pen += advance;
    }
    return i;
}
