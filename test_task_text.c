#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "task_text.h"

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* Row range holding ink, e.g. "." sits low, "l" reaches high. */
static void ink_rows(const char *text, int *top, int *bottom)
{
    TaskBitmap bitmap;
    *top = -1;
    *bottom = -1;
    if (!task_label_bitmap(text, &bitmap)) {
        return;
    }

    for (int y = 0; y < bitmap.height; y++) {
        for (int x = 0; x < bitmap.width; x++) {
            if (bitmap.pixels[((size_t)y * bitmap.width + x) * 4 + 3] == 0) {
                continue;
            }
            if (*top < 0) {
                *top = y;
            }
            *bottom = y;
        }
    }
    task_bitmap_free(&bitmap);
}

int main(void)
{
    /* Glyphs keep their own width: "iii" is narrower than "WWW". */
    check(task_text_offset("iii", 3) < task_text_offset("WWW", 3), "proportional widths");

    /* Glyphs keep their size and baseline instead of filling the cell. */
    int dot_top, dot_bottom, l_top, l_bottom, p_top, p_bottom;
    ink_rows(".", &dot_top, &dot_bottom);
    ink_rows("l", &l_top, &l_bottom);
    ink_rows("p", &p_top, &p_bottom);
    check(dot_top > l_top + 4, "period sits below ascender");
    check(p_bottom > l_bottom + 2, "descender drops below baseline");

    /* A pointer at a cursor's offset lands on that cursor. */
    const char *text = "Walk the dog!";
    for (size_t i = 0; i <= strlen(text); i++) {
        check(task_text_cursor(text, task_text_offset(text, i)) == i, "cursor round trip");
    }
    check(task_text_cursor(text, -50) == 0, "cursor clamps left");
    check(task_text_cursor(text, 5000) == strlen(text), "cursor clamps right");

    /* A full-length, all-caps task still fits the input field. */
    const char *wide = "MOW THE LAWN AND WASH THE CAR!!!";
    check(strlen(wide) == TASK_TEXT_MAX, "wide task is full length");
    check(task_text_offset(wide, TASK_TEXT_MAX) <= TASK_INPUT_TEXT_WIDTH, "full task fits");

    if (failures) {
        return EXIT_FAILURE;
    }
    printf("task text checks passed\n");
    return EXIT_SUCCESS;
}
