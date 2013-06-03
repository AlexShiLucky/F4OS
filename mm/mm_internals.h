#ifndef MM_INTERNALS_INCLUDED
#define MM_INTERNALS_INCLUDED

#include <kernel/semaphore.h>

struct heapnode {
    uint8_t order;
    struct heapnode *next;
};

struct buddy {
    uint8_t max_order;
    uint8_t min_order;
    struct semaphore semaphore;
    struct heapnode **list;
};

extern struct buddy user_buddy;
extern struct heapnode *user_buddy_list[];       /* Top is buddy_list[17], for locations 2^17 (128kb) in size */

extern struct buddy kernel_buddy;
extern struct heapnode *kernel_buddy_list[];

#ifdef CONFIG_MM_PROFILING
extern uint64_t begin_malloc_timestamp, end_malloc_timestamp;
#endif

#endif
