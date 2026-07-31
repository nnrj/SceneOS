#ifndef SCENE_HEAP_H
#define SCENE_HEAP_H

#include <stdbool.h>
#include <stddef.h>

bool scene_heap_init(void);
void *scene_heap_allocate(size_t size, size_t alignment);
bool scene_heap_free(void *address);
size_t scene_heap_committed_pages(void);
size_t scene_heap_allocated_bytes(void);

#endif
