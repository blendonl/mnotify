#include "tests.h"
#include "anim.h"

static void test_progress(void) {
    CHECK(anim_progress(0, 200) == 0.0, "starts at 0");
    CHECK(anim_progress(100, 200) == 0.5, "halfway");
    CHECK(anim_progress(200, 200) == 1.0, "ends at 1");
    CHECK(anim_progress(500, 200) == 1.0, "clamped above");
    CHECK(anim_progress(-10, 200) == 0.0, "clamped below");
    CHECK(anim_progress(0, 0) == 1.0, "no duration means done");
}

static void test_easing(void) {
    const Easing all[] = { EASE_LINEAR, EASE_OUT, EASE_IN_OUT };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        CHECK(anim_ease(all[i], 0.0) == 0.0, "easing %zu starts at 0", i);
        CHECK(anim_ease(all[i], 1.0) == 1.0, "easing %zu ends at 1", i);
        double prev = 0.0;
        bool   rising = true;
        for (int step = 1; step <= 100; step++) {
            double v = anim_ease(all[i], step / 100.0);
            if (v < prev) rising = false;
            prev = v;
        }
        CHECK(rising, "easing %zu never goes backwards", i);
    }
    CHECK(anim_ease(EASE_LINEAR, 0.25) == 0.25, "linear is linear");
    CHECK(anim_ease(EASE_OUT, 0.5) > 0.5, "ease_out is ahead at the midpoint");
    CHECK(anim_ease(EASE_IN_OUT, 0.5) == 0.5, "ease_in_out is symmetric");
    CHECK(anim_ease(EASE_OUT, -1.0) == 0.0 && anim_ease(EASE_OUT, 2.0) == 1.0, "easing clamps");
}

static void test_lerp(void) {
    CHECK(anim_lerp(10, 20, 0.0) == 10, "lerp start");
    CHECK(anim_lerp(10, 20, 1.0) == 20, "lerp end");
    CHECK(anim_lerp(10, 20, 0.5) == 15, "lerp middle");
    CHECK(anim_lerp(-20, -10, 0.5) == -15, "lerp negative");
    CHECK(anim_lerp(100, 0, 0.25) == 75, "lerp downwards");
}

static void test_slide(void) {
    int dx, dy;
    anim_slide_offset(CORNER_BOTTOM_RIGHT, 32, &dx, &dy);
    CHECK(dx == 32 && dy == 0, "right corners slide in from the right");
    anim_slide_offset(CORNER_TOP_LEFT, 32, &dx, &dy);
    CHECK(dx == -32 && dy == 0, "left corners slide in from the left");
    anim_slide_offset(CORNER_TOP_CENTER, 32, &dx, &dy);
    CHECK(dx == 0 && dy == -32, "top-center slides down from above");
    anim_slide_offset(CORNER_BOTTOM_CENTER, 32, &dx, &dy);
    CHECK(dx == 0 && dy == 32, "bottom-center slides up from below");
}

int main(void) {
    test_progress();
    test_easing();
    test_lerp();
    test_slide();
    return tests_report("anim");
}
