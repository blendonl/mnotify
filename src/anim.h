#ifndef MNOTIFY_ANIM_H
#define MNOTIFY_ANIM_H

#include "config_types.h"

double anim_progress(double elapsed_ms, int duration_ms);
double anim_ease(Easing easing, double t);
int    anim_lerp(int from, int to, double t);
void   anim_slide_offset(Corner corner, int distance, int *dx, int *dy);

#endif
