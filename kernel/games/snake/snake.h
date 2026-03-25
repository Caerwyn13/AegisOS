#ifndef SNAKE_H
#define SNAKE_H

#include "types.h"
#include "vga.h"

#define SNAKE_SIZE 16
#define MAX_SNAKE_LENGTH 64

typedef struct {
    uint32_t x, y;
} point_t;

typedef struct {
    point_t body[MAX_SNAKE_LENGTH];
    uint32_t length;
    int dx, dy;
} snake_t;

extern snake_t snake;
extern point_t food;

void snake_init();
void snake_update();
void snake_draw();
void place_food();

#endif