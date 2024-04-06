#ifndef AALLOC_H
#define AALLOC_H

#include "acommon.h"

void *arealloc(void *ptr, memmax size);
#define amalloc(size) arealloc(NULL, size)
void *acalloc(memmax elem, memmax size);
void afree(void *ptr);

void panic(const char *errmsg, ...);
void cleanup_all(void);
void cleanup_fork(void);

#endif
