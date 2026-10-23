#ifndef E1000_H
#define E1000_H

#include "types.h"

#define E1000_MAC_LENGTH 6

int e1000_init(uint8_t mac[E1000_MAC_LENGTH]);
int e1000_send(const uint8_t *packet, uint16_t length);
int e1000_receive(uint8_t *packet, uint16_t capacity);

#endif
