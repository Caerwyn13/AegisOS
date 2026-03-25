#include "random.h"

// Single global RNG state
static uint32_t state = 1;

void srand(uint32_t seed) {
    state = seed;
}

uint32_t rand(void) {
    // Simple linear congruential generator (integer only)
    state = state * 1664525 + 1013904223;
    return state;
}