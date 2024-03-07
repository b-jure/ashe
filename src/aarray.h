#ifndef AARRAY_H
#define AARRAY_H

/* [======== GENERIC ARRAY =========] */

#include "aalloc.h"

#include <memory.h>
#include <stdlib.h>



#define ARRAY_INITIAL_SIZE 8
#define GROW_ARRAY_CAPACITY(cap, initial_size) ((cap) < (initial_size) ? (initial_size) : (cap) * 2)


/* These are internal only macros. */
#define _ARRAY_METHOD_NAME(tname, name) tname##_##name
#define _CALL_ARRAY_METHOD(tname, name, ...) \
    _ARRAY_METHOD_NAME(tname, name)(self __VA_OPT__(,) __VA_ARGS__)
#define _ARRAY_METHOD(tname, name, ...) \
    _ARRAY_METHOD_NAME(tname, name) (tname * self __VA_OPT__(,) __VA_ARGS__)
#define _ARRAY_METHOD_CONST(tname, name, ...) \
    _ARRAY_METHOD_NAME(tname, name) (const tname * self __VA_OPT__(,) __VA_ARGS__)



typedef void (*FreeFn)(void* value);

/* Create new 'name' array with 'type' elements */
#define ARRAY_NEW(name, type)                                                                      \
    typedef struct {                                                                               \
        uint32 cap;                                                                                \
        uint32 len;                                                                                \
        type* data;                                                                                \
    } name;                                                                                        \
                                                                                                   \
    static finline void _ARRAY_METHOD(name, init)                                                  \
    {                                                                                              \
        self->cap = 0;                                                                             \
        self->len = 0;                                                                             \
        self->data = NULL;                                                                         \
    }                                                                                              \
                                                                                                   \
    static finline void _ARRAY_METHOD(name, init_cap, uint32 cap)                                  \
    {                                                                                              \
        self->data = (type*)aalloc(self->data, cap * sizeof(type));                                \
        self->cap = cap;                                                                           \
    }                                                                                              \
                                                                                                   \
    static finline void _ARRAY_METHOD(name, grow)                                                  \
    {                                                                                              \
        uint32 oldcap = self->cap;                                                                 \
        if(oldcap != 0 && !ispow2(oldcap)) {                                                       \
            oldcap |= (oldcap >> 1);                                                               \
            oldcap |= (oldcap >> 2);                                                               \
            oldcap |= (oldcap >> 4);                                                               \
            oldcap |= (oldcap >> 8);                                                               \
            oldcap |= (oldcap >> 16);                                                              \
            oldcap++;                                                                              \
            oldcap >>= 1;                                                                          \
        }                                                                                          \
        self->cap = MIN(GROW_ARRAY_CAPACITY(oldcap, ARRAY_INITIAL_SIZE), UINT_MAX);                \
        if(unlikely(self->cap >= UINT_MAX)) {                                                      \
            panic("[%s:%d] Capacity exceeded in array '%s'! (Limit: %u)\n",                        \
                    __FILE__, __LINE__, #name, UINT_MAX >> 1);                                     \
        } else {                                                                                   \
            self->data = (type*)aalloc(self->data, self->cap * sizeof(type));                      \
        }                                                                                          \
    }                                                                                              \
                                                                                                   \
    static finline uint32 _ARRAY_METHOD(name, push, type value)                                    \
    {                                                                                              \
        if(self->cap <= self->len) _CALL_ARRAY_METHOD(name, grow);                                 \
        self->data[self->len++] = value;                                                           \
        return self->len - 1;                                                                      \
    }                                                                                              \
                                                                                                   \
    static finline type _ARRAY_METHOD(name, pop)                                                   \
    {                                                                                              \
        return self->data[--self->len];                                                            \
    }                                                                                              \
                                                                                                   \
    static finline type* _ARRAY_METHOD_CONST(name, index, uint32 idx)                                  \
    {                                                                                              \
        return &self->data[idx];                                                                 \
    }                                                                                              \
                                                                                                   \
    static finline type* _ARRAY_METHOD_CONST(name, last)                                                 \
    {                                                                                              \
        return &self->data[self->len - 1];                                                         \
    }                                                                                              \
                                                                                                   \
    static finline type* _ARRAY_METHOD_CONST(name, first)                                                \
    {                                                                                              \
        return &self->data[0];                                                                     \
    }                                                                                              \
                                                                                                   \
    static finline void _ARRAY_METHOD(name, insert, uint32 index, type value)                      \
    {                                                                                              \
        type* src = self->data + index;                                                            \
        type* dest = src + 1;                                                                      \
        memmove(dest, src, self->len - index);                                                     \
        self->len++;                                                                               \
        self->data[index] = value;                                                                 \
    }                                                                                              \
                                                                                                   \
    static finline type _ARRAY_METHOD(name, remove, uint32 index)                                  \
    {                                                                                              \
        if(self->len == 1) return _CALL_ARRAY_METHOD(name, pop);                                   \
        type* src = self->data + index;                                                            \
        type* dest = src - 1;                                                                      \
        type retval = *src;                                                                        \
        memcpy(dest, src, self->len - index);                                                     \
        self->len--;                                                                               \
        return retval;                                                                             \
    }                                                                                              \
                                                                                                   \
    static finline void _ARRAY_METHOD(name, ensure, unsigned int len)                              \
    {                                                                                              \
        while(self->cap < self->len + len)                                                         \
            _CALL_ARRAY_METHOD(name, grow);                                                        \
    }                                                                                              \
                                                                                                   \
    static finline void _ARRAY_METHOD(name, free, FreeFn fn)                                       \
    {                                                                                              \
        if(fn)                                                                             \
            for(uint32 i = 0; i < self->len; i++)                                                  \
                fn((void*)&self->data[i]);                                                         \
        if(self->data) aalloc(self->data, 0);                                              \
    }                                                                                              \
                                                                                                   \
    static finline void _ARRAY_METHOD(name, push_str, const char* str, memmax len)                 \
    {                                                                                              \
        uint32 required = self->len + ++len;                                                       \
        if(self->cap < required) _CALL_ARRAY_METHOD(name, ensure, required);                       \
        type* dest = self->data + self->len;                                                       \
        memcpy(dest, str, len);                                                                    \
        self->len += len;                                                                          \
    }

#endif
