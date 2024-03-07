#ifndef AALLOC_H
#define AALLOC_H

#include "acommon.h"

void* aalloc(void* ptr, memmax size);
void panic(const char* errmsg, ...);

#endif
