#include <stdbool.h>
#include <stdio.h>

#include "core/post_fx_debug_internal.inl"

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

int main(void) {
    CHECK(PostFX_ApplyBloomOverrideValue(true, -1.0f), "negative override keeps enabled state");
    CHECK(!PostFX_ApplyBloomOverrideValue(false, -1.0f), "negative override keeps disabled state");
    CHECK(!PostFX_ApplyBloomOverrideValue(true, 0.0f), "zero forces bloom off");
    CHECK(PostFX_ApplyBloomOverrideValue(false, 1.0f), "one forces bloom on");
    puts("postfx_bloom_override_test: PASS");
    return 0;
}
