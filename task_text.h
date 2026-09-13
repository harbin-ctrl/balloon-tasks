#ifndef BALLOON_TASKS_TEXT_H
#define BALLOON_TASKS_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TASK_TEXT_MAX = 32,
    TASK_TEXT_CHAR_WIDTH = 16,
    TASK_PANEL_WIDTH = 560,
    TASK_PANEL_HEIGHT = 140,
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

#endif
