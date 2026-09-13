#include "task_text.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "task_font.h"

typedef struct {
    uint8_t r, g, b, a;
} Color;

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

static uint8_t glyph_pixel(unsigned char c, int x, int y)
{
    if (c < 32 || c > 126 || x < 0 || x >= TASK_GLYPH_WIDTH ||
            y < 0 || y >= TASK_GLYPH_HEIGHT) {
        return 0;
    }
    return task_font_bitmap[c - 32][y][x];
}

static void glyph(TaskBitmap *bitmap, unsigned char c, int x, int y, int scale, Color color)
{
    uint8_t base_alpha = color.a;
    for (int row = 0; row < TASK_GLYPH_HEIGHT; row++) {
        for (int col = 0; col < TASK_GLYPH_WIDTH; col++) {
            uint8_t alpha = glyph_pixel(c, col, row);
            if (!alpha) {
                continue;
            }
            Color pixel_color = color;
            pixel_color.a = (uint8_t)((base_alpha * alpha + 127) / 255);
            fill(bitmap, x + col * scale, y + row * scale, scale, scale, pixel_color);
        }
    }
}

static void text(TaskBitmap *bitmap, const char *value, int x, int y, int scale,
                 Color color, Color outline)
{
    for (size_t i = 0; value[i]; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < 32 || c > 126) {
            c = '?';
        }
        int gx = x + (int)i * TASK_GLYPH_WIDTH * scale;
        glyph(bitmap, c, gx - 1, y, scale, outline);
        glyph(bitmap, c, gx + 1, y, scale, outline);
        glyph(bitmap, c, gx, y - 1, scale, outline);
        glyph(bitmap, c, gx, y + 1, scale, outline);
        glyph(bitmap, c, gx, y, scale, color);
    }
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
    const int scale = 1;
    int width = (int)strlen(value) * TASK_GLYPH_WIDTH * scale + 2;
    if (!bitmap_alloc(bitmap, width, TASK_GLYPH_HEIGHT * scale + 2)) {
        return false;
    }
    text(bitmap, value, 1, 1, scale,
    (Color) {
        255, 255, 255, 255
    }, (Color) {
        4, 24, 63, 255
    });
    return true;
}

bool task_panel_bitmap(const char *value, bool active, bool has_started,
                       int tasks_left, TaskBitmap *bitmap)
{
    if (!bitmap_alloc(bitmap, TASK_PANEL_WIDTH, TASK_PANEL_HEIGHT)) {
        return false;
    }

    fill(bitmap, 0, 0, bitmap->width, bitmap->height, (Color) {
        7, 19, 42, 225
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
    text(bitmap, "New Task:", 14, 10, 1,
    (Color) {
        255, 255, 255, 255
    }, (Color) {
        3, 12, 30, 255
    });
    text(bitmap, "ENTER TO ADD", 354, 12, 1,
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
    fill(bitmap, 14, 42, 532, 44, border);
    fill(bitmap, 17, 45, 526, 38, (Color) {
        245, 248, 255, 245
    });
    text(bitmap, value, 23, 52, 1,
    (Color) {
        10, 28, 58, 255
    }, (Color) {
        255, 255, 255, 255
    });
    if (active) {
        int cursor_x = 23 + (int)strlen(value) * 16;
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
    int status_x = (TASK_PANEL_WIDTH - (int)strlen(status) * TASK_GLYPH_WIDTH) / 2;
    text(bitmap, status, status_x, 104, 1,
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
