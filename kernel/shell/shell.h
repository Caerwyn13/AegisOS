#ifndef SHELL_H
#define SHELL_H

#define ARROW_UP   0x48
#define ARROW_DOWN 0x50

void shell_init();
void shell_handle_key(char c);

#endif // SHELL_H