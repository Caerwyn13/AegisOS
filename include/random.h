#ifndef RANDOM_H
#define RANDOM_H

#include "types.h"

// Seed the RNG
void srand(uint32_t seed);

// Get next random number
uint32_t rand(void);

#endif