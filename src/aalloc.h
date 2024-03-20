#ifndef AALLOC_H
#define AALLOC_H

#include "acommon.h"

typedef void (*CleanupFn)(void);

void *arealloc(void *ptr, memmax size);
#define afree(ptr) arealloc(ptr, 0)
#define amalloc(size) arealloc(NULL, size)

void panic(const char *errmsg, ...);
void cleanup_all(void);
void cleanup_fork(void);

#endif
