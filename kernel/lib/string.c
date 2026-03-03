#include "string.h"

int strlen(const char* str) {
    int i = 0;
    while (str[i]) i++;
    return i;
}

int strcmp(const char* a, const char* b) {
    while(*a && *b && *a == *b) {
        a++; b++;
    }
    return *a - *b;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while((*d++ = *src++));
    return dst;
}

char* strcat(char* dst, const char* src) {
    char* d = dst + strlen(dst);
    while ((*d++ = *src++));
    return dst;
}

void* memset(void* ptr, int val, uint32_t len) {
    uint8_t* p = ptr;
    while (len--) *p++ = val;
    return ptr;
}

void* memcpy(void* dst, const void*src, uint32_t len) {
    uint8_t* d = dst;
    const uint8_t* s = src;
    while (len--) *d++ = *s++;
    return dst;
}