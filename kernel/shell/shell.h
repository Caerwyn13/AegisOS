#ifndef SHELL_H
#define SHELL_H

#include "multiboot.h"

#define ARROW_UP    0x100
#define ARROW_DOWN  0x101
#define ARROW_LEFT  0x102
#define ARROW_RIGHT 0x103
#define KEY_HOME    0x104
#define KEY_END     0x105
#define KEY_DELETE  0x106

void shell_init(multiboot_info_t *mbi);
void shell_handle_key(uint16_t key);
void shell_process_input(void);

#endif // SHELL_H