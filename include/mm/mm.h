#ifndef MM_MM_H_INCLUDED
#define MM_MM_H_INCLUDED

#include <stdint.h>

#define MM_HEADER_SIZE   sizeof(uint32_t)

void init_heap(void) __attribute__((section(".kernel")));
void *kmalloc(uint32_t size) __attribute__((malloc,section(".kernel")));
void kfree(void *address) __attribute__((section(".kernel")));

uint32_t mm_space(void) __attribute__((section(".kernel")));
uint32_t mm_kspace(void) __attribute__((section(".kernel")));

#endif
