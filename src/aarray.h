#ifndef AARRAY_H
#define AARRAY_H

/* [======== GENERIC ARRAY =========] */

#include "aalloc.h"

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>

#define ARRAY_INITIAL_SIZE 8
#define GROW_ARRAY_CAPACITY(cap, initial_size) \
	((cap) < (initial_size) ? (initial_size) : (cap) * 2)

/* These are internal only macros. */
#define _ARRAY_METHOD_NAME(tname, name) tname##_##name
/* call array method */
#define _CALL_ARRAY_METHOD(tname, name) _ARRAY_METHOD_NAME(tname, name)(self)
/* call array vararg method */
#define _CALL_ARRAY_METHOD_VARARG(tname, name, ...) \
	_ARRAY_METHOD_NAME(tname, name)(self, __VA_ARGS__)
/* array method declaration */
#define _ARRAY_METHOD(tname, name) _ARRAY_METHOD_NAME(tname, name)(tname * self)
/* array method vararg declaration */
#define _ARRAY_METHOD_VARARG(tname, name, ...) \
	_ARRAY_METHOD_NAME(tname, name)(tname * self, __VA_ARGS__)
/* array method declaration with 'self' as const */
#define _ARRAY_METHOD_CONST(tname, name) \
	_ARRAY_METHOD_NAME(tname, name)(const tname *self)
/* array method vararg declaration with 'self' as const */
#define _ARRAY_METHOD_CONST_VARARG(tname, name, ...) \
	_ARRAY_METHOD_NAME(tname, name)(const tname *self, __VA_ARGS__)

typedef void (*FreeFn)(void *value);

/* Create new 'name' array with 'type' elements */
#define ARRAY_NEW(name, type)                                                              \
	typedef struct {                                                                   \
		uint32 cap;                                                                \
		uint32 len;                                                                \
		type *data;                                                                \
	} name;                                                                            \
                                                                                           \
	static inline void _ARRAY_METHOD(name, init)                                       \
	{                                                                                  \
		self->cap = 0;                                                             \
		self->len = 0;                                                             \
		self->data = NULL;                                                         \
	}                                                                                  \
                                                                                           \
	static inline uint32 _ARRAY_METHOD(name, len)                                      \
	{                                                                                  \
		return self->len;                                                          \
	}                                                                                  \
                                                                                           \
	static inline uint32 _ARRAY_METHOD(name, cap)                                      \
	{                                                                                  \
		return self->cap;                                                          \
	}                                                                                  \
                                                                                           \
	static inline void _ARRAY_METHOD_VARARG(name, init_cap, uint32 cap)                \
	{                                                                                  \
		_CALL_ARRAY_METHOD(name, init);                                            \
		self->data = (type *)arealloc(self->data, cap * sizeof(type));             \
		self->cap = cap;                                                           \
	}                                                                                  \
                                                                                           \
	static void _ARRAY_METHOD(name, grow)                                              \
	{                                                                                  \
		uint32 oldcap = self->cap;                                                 \
		if (unlikely(oldcap >= UINT_MAX)) {                                        \
			panic("[%s:%d] Capacity limit reached in array '%s'! (Limit: %u)", \
			      __FILE__, __LINE__, #name, UINT_MAX);                        \
		}                                                                          \
		if (oldcap != 0 && !ispow2(oldcap)) {                                      \
			oldcap |= (oldcap >> 1);                                           \
			oldcap |= (oldcap >> 2);                                           \
			oldcap |= (oldcap >> 4);                                           \
			oldcap |= (oldcap >> 8);                                           \
			oldcap |= (oldcap >> 16);                                          \
			oldcap++;                                                          \
			oldcap >>= 1;                                                      \
		}                                                                          \
		self->cap =                                                                \
			MIN(GROW_ARRAY_CAPACITY(oldcap, ARRAY_INITIAL_SIZE),               \
			    UINT_MAX);                                                     \
		self->data = (type *)arealloc(self->data,                                  \
					      self->cap * sizeof(type));                   \
	}                                                                                  \
                                                                                           \
	static inline uint32 _ARRAY_METHOD_VARARG(name, push, type value)                  \
	{                                                                                  \
		if (self->cap <= self->len)                                                \
			_CALL_ARRAY_METHOD(name, grow);                                    \
		self->data[self->len++] = value;                                           \
		return self->len - 1;                                                      \
	}                                                                                  \
                                                                                           \
	static inline type _ARRAY_METHOD(name, pop)                                        \
	{                                                                                  \
		return self->data[--self->len];                                            \
	}                                                                                  \
                                                                                           \
	static inline type *_ARRAY_METHOD_CONST_VARARG(name, index,                        \
						       uint32 idx)                         \
	{                                                                                  \
		return &self->data[idx];                                                   \
	}                                                                                  \
                                                                                           \
	static inline type *_ARRAY_METHOD_CONST(name, last)                                \
	{                                                                                  \
		return &self->data[self->len - 1];                                         \
	}                                                                                  \
                                                                                           \
	static inline type *_ARRAY_METHOD_CONST(name, first)                               \
	{                                                                                  \
		return &self->data[0];                                                     \
	}                                                                                  \
                                                                                           \
	static inline void _ARRAY_METHOD_VARARG(name, ensure, uint32 len)                  \
	{                                                                                  \
		while (self->cap < self->len + len)                                        \
			_CALL_ARRAY_METHOD(name, grow);                                    \
	}                                                                                  \
                                                                                           \
	static inline void _ARRAY_METHOD_VARARG(name, insert, uint32 index,                \
						type value)                                \
	{                                                                                  \
		if (index == self->len) {                                                  \
			_CALL_ARRAY_METHOD_VARARG(name, push, value);                      \
			return;                                                            \
		}                                                                          \
		_CALL_ARRAY_METHOD_VARARG(name, ensure, 1);                                \
		type *src = self->data + index;                                            \
		memmove(src + 1, src, (self->len - index) * sizeof(type));                 \
		self->len++;                                                               \
		self->data[index] = value;                                                 \
	}                                                                                  \
                                                                                           \
	static inline type _ARRAY_METHOD_VARARG(name, remove, uint32 index)                \
	{                                                                                  \
		type *dest;                                                                \
		type retval;                                                               \
		if (index == self->len)                                                    \
			return _CALL_ARRAY_METHOD(name, pop);                              \
		dest = self->data + index;                                                 \
		retval = *dest;                                                            \
		memmove(dest, dest + 1,                                                    \
			(self->len - index - 1) * sizeof(type));                           \
		self->len--;                                                               \
		return retval;                                                             \
	}                                                                                  \
                                                                                           \
	static inline void _ARRAY_METHOD_VARARG(name, free, FreeFn fn)                     \
	{                                                                                  \
		uint32 i;                                                                  \
                                                                                           \
		if (fn != NULL)                                                            \
			for (i = 0; i < self->len; i++)                                    \
				fn((void *)&self->data[i]);                                \
		if (self->cap > 0) {                                                       \
			ashe_assert(self->data != NULL);                                   \
			afree(self->data);                                                 \
		}                                                                          \
	}                                                                                  \
                                                                                           \
	static inline void _ARRAY_METHOD_VARARG(name, push_str,                            \
						const char *str, memmax len)               \
	{                                                                                  \
		uint32 required;                                                           \
                                                                                           \
		ashe_assert(sizeof(type) == sizeof(char));                                 \
		if (len <= 0)                                                              \
			return;                                                            \
		required = self->len + len;                                                \
		if (self->cap < required)                                                  \
			_CALL_ARRAY_METHOD_VARARG(name, ensure, required);                 \
		type *dest = self->data + self->len;                                       \
		memcpy(dest, str, len * sizeof(char));                                     \
		self->len += len;                                                          \
	}                                                                                  \
                                                                                           \
	static inline void _ARRAY_METHOD_VARARG(name, push_ptr,                            \
						const void *ptr)                           \
	{                                                                                  \
		char temp[UINT_DIGITS];                                                    \
		ssize chars;                                                               \
                                                                                           \
		ashe_assert(sizeof(type) == sizeof(char));                                 \
		chars = snprintf(temp, sizeof(temp), "%p", ptr);                           \
		if (unlikely(chars < 0 || (memmax)chars > sizeof(temp)))                   \
			panic(#name "_puhs_ptr(%p) failed.", ptr);                         \
		_CALL_ARRAY_METHOD_VARARG(name, push_str, temp, chars);                    \
	}                                                                                  \
                                                                                           \
	static inline void _ARRAY_METHOD_VARARG(name, push_num, ssize n)                   \
	{                                                                                  \
		char temp[UINT_DIGITS];                                                    \
		ssize chars;                                                               \
                                                                                           \
		ashe_assert(sizeof(type) == sizeof(char));                                 \
		chars = snprintf(temp, sizeof(temp), "%ld", n);                            \
		if (unlikely(chars < 0 || (memmax)chars > sizeof(temp)))                   \
			panic(#name "_puhs_num(%u) failed.", n);                           \
		_CALL_ARRAY_METHOD_VARARG(name, push_str, temp, chars);                    \
	}

#endif
