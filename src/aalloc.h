#ifndef AALLOC_H
#define AALLOC_H

#include "acommon.h"

typedef void (*CleanupFn)(void);

void* aalloc(void* ptr, memmax size);
void panic(const char* errmsg, ...);

void cleanup_all(void);
void cleanup_fork(void);

#endif
