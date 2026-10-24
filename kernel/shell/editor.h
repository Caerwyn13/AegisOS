#ifndef EDITOR_H
#define EDITOR_H

#include "types.h"

int editor_start(const char *filename);
int editor_is_active(void);
void editor_handle_key(uint16_t key);

#endif
