#ifndef BALLOON_TASKS_TEXT_H
#define BALLOON_TASKS_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TASK_TEXT_MAX = 32,
    TASK_PANEL_WIDTH = 672,
    TASK_PANEL_HEIGHT = 140,
    TASK_INPUT_TEXT_X = 23,
    TASK_INPUT_TEXT_WIDTH = TASK_PANEL_WIDTH - 2 * TASK_INPUT_TEXT_X,
};

typedef struct {
    uint8_t *pixels;
    int width;
    int height;
} TaskBitmap;

bool task_label_bitmap(const char *text, TaskBitmap *bitmap);
bool task_panel_bitmap(const char *text, bool active, bool has_started,
                      int tasks_left, size_t cursor, bool close_pressed,
                      TaskBitmap *bitmap);
void task_bitmap_free(TaskBitmap *bitmap);

/* Input field geometry: pixel x of a cursor, and the cursor nearest a pixel x. */
int task_text_offset(const char *text, size_t cursor);
size_t task_text_cursor(const char *text, int offset);

#endif
