#ifndef PORTS_H
#define PORTS_H

void outb(unsigned short port, unsigned char val);
unsigned char inb(unsigned short port);
void outw(uint16_t port, uint16_t val);
uint16_t inw(uint16_t port);

#endif // PORTS_H
