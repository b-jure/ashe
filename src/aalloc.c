#include "aalloc.h"
#include "ajobcntl.h"
#include "ashell.h"

#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>



void cleanup_all(void)
{
    JobControl_free_and_harvest(&ashe.sh_jobcntl);
    ArrayCharptr_free(&ashe.sh_buffers, NULL);
    ArrayConditional_free(&ashe.sh_conds, (FreeFn) Conditional_free);
}


void cleanup_fork(void)
{
    JobControl_free(&ashe.sh_jobcntl);
    ArrayCharptr_free(&ashe.sh_buffers, NULL);
    ArrayConditional_free(&ashe.sh_conds, (FreeFn) Conditional_free);
}


void panic(const char* errmsg, ...)
{
    va_list argp;
    if(errmsg) {
        va_start(argp, errmsg);
        vfprintf(stderr, errmsg, argp);
        va_end(argp);
    }
    if(ashe.sh_flags.isfork) cleanup_fork();
    else cleanup_all();
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
        perror(ENOMEM);
        panic(NULL);
    }
    return ptr;
}
