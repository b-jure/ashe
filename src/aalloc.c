#include "aalloc.h"
#include "ashe_utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void panic(const char* errmsg, ...)
{
    va_list argp;
    if(errmsg) {
        va_start(argp, errmsg);
        vfprintf(stderr, errmsg, argp);
        va_end(argp);
    }
    Shell_cleanup();
    abort();
}

void* aalloc(void* ptr, memmax size)
{
    if(size == 0) {
        free(ptr);
        return NULL;
    }
    ptr = realloc(ptr, size);
    if(unlikely(ptr == NULL)) {
        panic("out of memory (tried allocating - '%lu')", size);
        // unreachable;
    }
    return ptr;
}
