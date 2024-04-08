#ifndef AALLOC_H
#define AALLOC_H

#include "acommon.h"

void *arealloc(void *ptr, memmax size);
#define amalloc(size) arealloc(NULL, size)
void *acalloc(memmax elem, memmax size);
void afree(void *ptr);

void ashe_cleanup(void);
void ashe_cleanupfork(void);

/* NEVER call 'ashe_internal_panic()' directly, use macros instead */
void ashe_internal_panic(ubyte direct, const char *errmsg, ...);
#define ashe_panic(err) ashe_internal_panic(0, err, __func__);
#define ashe_panicf(errfmt, ...) \
	ashe_internal_panic(0, errfmt, __func__, __VA_ARGS__)

#endif
