#ifndef STRING_H
#define STRING_H

#include "types.h"

int   strlen(const char* str);
int   strcmp(const char* a, const char* b);
char* strcpy(char* dst, const char* src);
char* strcat(char* dst, const char* src);
void* memset(void* ptr, int val, uint32_t len);
void* memcpy(void*dst, const void* src, uint32_t len);

#endif // STRING_H