#include "snake.h"

snake_t snake;
point_t food;

// ---------------------------------------------------
// Initialize snake
// ---------------------------------------------------
void snake_init() {
    snake.length = 3;
    snake.dx = SNAKE_SIZE;
    snake.dy = 0;

    // Start in center
    uint32_t start_x = VGA_13H_WIDTH / 2 / SNAKE_SIZE * SNAKE_SIZE;
    uint32_t start_y = VGA_13H_HEIGHT / 2 / SNAKE_SIZE * SNAKE_SIZE;

    for(uint32_t i = 0; i < snake.length; i++) {
        snake.body[i].x = start_x - i * SNAKE_SIZE;
        snake.body[i].y = start_y;
    }

    place_food();
}

// ---------------------------------------------------
// Place food safely within bounds
// ---------------------------------------------------
void place_food() {
    // Fixed values for now (no rand)
    food.x = (184739503 % (VGA_13H_WIDTH / SNAKE_SIZE)) * SNAKE_SIZE;
    food.y = (403928425 % (VGA_13H_HEIGHT / SNAKE_SIZE)) * SNAKE_SIZE;
}

// ---------------------------------------------------
// Update snake position
// ---------------------------------------------------
void snake_update() {
    // Move body
    for(uint32_t i = snake.length - 1; i > 0; i--) {
        snake.body[i] = snake.body[i-1];
    }

    // Move head
    snake.body[0].x += snake.dx;
    snake.body[0].y += snake.dy;

    // Wrap around screen
    if(snake.body[0].x >= VGA_13H_WIDTH)  snake.body[0].x = 0;
    if(snake.body[0].y >= VGA_13H_HEIGHT) snake.body[0].y = 0;
    if((int32_t)snake.body[0].x < 0)     snake.body[0].x = VGA_13H_WIDTH - SNAKE_SIZE;
    if((int32_t)snake.body[0].y < 0)     snake.body[0].y = VGA_13H_HEIGHT - SNAKE_SIZE;

    // Check for food collision
    if(snake.body[0].x == food.x && snake.body[0].y == food.y) {
        if(snake.length < MAX_SNAKE_LENGTH) snake.length++;
        place_food();
    }
}

// ---------------------------------------------------
// Draw snake and food safely
// ---------------------------------------------------
void snake_draw() {
    // Clear screen
    vga_clear();

    // Draw snake
    for(uint32_t i = 0; i < snake.length; i++) {
        vga_draw_rect(
            snake.body[i].x,
            snake.body[i].y,
            SNAKE_SIZE,
            SNAKE_SIZE,
            15 // White
        );
    }

    // Draw food
    vga_draw_rect(
        food.x,
        food.y,
        SNAKE_SIZE,
        SNAKE_SIZE,
        12 // Red
    );
}