#ifndef BALLOON_TASKS_TITLE_H
#define BALLOON_TASKS_TITLE_H

#include <stdbool.h>

/* The title screen: "BALLOON TASKS!" on a gentle arc, faded in and out.
   Needs a current GL context. */
typedef struct Title Title;

Title *title_create(void);
void title_destroy(Title *title);

/* True until the title has faded out, elapsed seconds after it began. */
bool title_playing(double elapsed);

void title_draw(const Title *title, double elapsed, int width, int height);

#endif
