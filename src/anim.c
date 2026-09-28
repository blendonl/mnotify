#include "anim.h"

double anim_progress(double elapsed_ms, int duration_ms) {
    if (duration_ms <= 0 || elapsed_ms >= duration_ms) return 1.0;
    if (elapsed_ms <= 0) return 0.0;
    return elapsed_ms / duration_ms;
}

double anim_ease(Easing easing, double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;

    switch (easing) {
    case EASE_OUT: {
        double u = 1.0 - t;
        return 1.0 - u * u * u;
    }
    case EASE_IN_OUT: {
        if (t < 0.5) return 4.0 * t * t * t;
        double u = 2.0 - 2.0 * t;
        return 1.0 - u * u * u / 2.0;
    }
    default:
        return t;
    }
}

int anim_lerp(int from, int to, double t) {
    double v = from + (double)(to - from) * t;
    return (int)(v < 0 ? v - 0.5 : v + 0.5);
}

void anim_slide_offset(Corner corner, int distance, int *dx, int *dy) {
    *dx = 0;
    *dy = 0;
    switch (corner) {
    case CORNER_TOP_RIGHT:
    case CORNER_BOTTOM_RIGHT:  *dx = distance;  break;
    case CORNER_TOP_LEFT:
    case CORNER_BOTTOM_LEFT:   *dx = -distance; break;
    case CORNER_TOP_CENTER:    *dy = -distance; break;
    case CORNER_BOTTOM_CENTER: *dy = distance;  break;
    }
}
