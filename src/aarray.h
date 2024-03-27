#ifndef AARRAY_H
#define AARRAY_H

/* [======== GENERIC ARRAY =========] */

#include "aalloc.h"

#include <memory.h>
#include <stdlib.h>

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
#define ARRAY_NEW(name, type)                                                           \
	typedef struct {                                                                \
		uint32 cap;                                                             \
		uint32 len;                                                             \
		type *data;                                                             \
	} name;                                                                         \
                                                                                        \
	static inline void _ARRAY_METHOD(name, init)                                    \
	{                                                                               \
		self->cap = 0;                                                          \
		self->len = 0;                                                          \
		self->data = NULL;                                                      \
	}                                                                               \
                                                                                        \
	static inline uint32 _ARRAY_METHOD(name, len)                                   \
	{                                                                               \
		return self->len;                                                       \
	}                                                                               \
                                                                                        \
	static inline uint32 _ARRAY_METHOD(name, cap)                                   \
	{                                                                               \
		return self->cap;                                                       \
	}                                                                               \
                                                                                        \
	static inline void _ARRAY_METHOD_VARARG(name, init_cap, uint32 cap)             \
	{                                                                               \
		_CALL_ARRAY_METHOD(name, init);                                         \
		self->data = (type *)arealloc(self->data, cap * sizeof(type));          \
		self->cap = cap;                                                        \
	}                                                                               \
                                                                                        \
	static void _ARRAY_METHOD(name, grow)                                           \
	{                                                                               \
		uint32 oldcap = self->cap;                                              \
		if (oldcap != 0 && !ispow2(oldcap)) {                                   \
			oldcap |= (oldcap >> 1);                                        \
			oldcap |= (oldcap >> 2);                                        \
			oldcap |= (oldcap >> 4);                                        \
			oldcap |= (oldcap >> 8);                                        \
			oldcap |= (oldcap >> 16);                                       \
			oldcap++;                                                       \
			oldcap >>= 1;                                                   \
		}                                                                       \
		self->cap =                                                             \
			MIN(GROW_ARRAY_CAPACITY(oldcap, ARRAY_INITIAL_SIZE),            \
			    UINT_MAX);                                                  \
		if (unlikely(self->cap >= UINT_MAX)) {                                  \
			panic("[%s:%d] Capacity exceeded in array '%s'! (Limit: %u)\n", \
			      __FILE__, __LINE__, #name, UINT_MAX >> 1);                \
		} else {                                                                \
			self->data = (type *)arealloc(                                  \
				self->data, self->cap * sizeof(type));                  \
		}                                                                       \
	}                                                                               \
                                                                                        \
	static inline uint32 _ARRAY_METHOD_VARARG(name, push, type value)               \
	{                                                                               \
		if (self->cap <= self->len)                                             \
			_CALL_ARRAY_METHOD(name, grow);                                 \
		self->data[self->len++] = value;                                        \
		return self->len - 1;                                                   \
	}                                                                               \
                                                                                        \
	static inline type _ARRAY_METHOD(name, pop)                                     \
	{                                                                               \
		return self->data[--self->len];                                         \
	}                                                                               \
                                                                                        \
	static inline type *_ARRAY_METHOD_CONST_VARARG(name, index,                     \
						       uint32 idx)                      \
	{                                                                               \
		return &self->data[idx];                                                \
	}                                                                               \
                                                                                        \
	static inline type *_ARRAY_METHOD_CONST(name, last)                             \
	{                                                                               \
		return &self->data[self->len - 1];                                      \
	}                                                                               \
                                                                                        \
	static inline type *_ARRAY_METHOD_CONST(name, first)                            \
	{                                                                               \
		return &self->data[0];                                                  \
	}                                                                               \
                                                                                        \
	static inline void _ARRAY_METHOD_VARARG(name, ensure, uint32 len)               \
	{                                                                               \
		while (self->cap < self->len + len)                                     \
			_CALL_ARRAY_METHOD(name, grow);                                 \
	}                                                                               \
                                                                                        \
	static inline void _ARRAY_METHOD_VARARG(name, insert, uint32 index,             \
						type value)                             \
	{                                                                               \
		if (index == self->len) {                                               \
			_CALL_ARRAY_METHOD_VARARG(name, push, value);                   \
			return;                                                         \
		}                                                                       \
		_CALL_ARRAY_METHOD_VARARG(name, ensure, 1);                             \
		type *src = self->data + index;                                         \
		type *dest = src + 1;                                                   \
		memmove(dest, src, self->len - index);                                  \
		self->len++;                                                            \
		self->data[index] = value;                                              \
	}                                                                               \
                                                                                        \
	static inline type _ARRAY_METHOD_VARARG(name, remove, uint32 index)             \
	{                                                                               \
		if (index == self->len)                                                 \
			return _CALL_ARRAY_METHOD(name, pop);                           \
		type *src = self->data + index;                                         \
		type *dest = src - 1;                                                   \
		type retval = *src;                                                     \
		memcpy(dest, src, self->len - index);                                   \
		self->len--;                                                            \
		return retval;                                                          \
	}                                                                               \
                                                                                        \
	static inline void _ARRAY_METHOD_VARARG(name, free, FreeFn fn)                  \
	{                                                                               \
		if (fn)                                                                 \
			for (uint32 i = 0; i < self->len; i++)                          \
				fn((void *)&self->data[i]);                             \
		if (self->data)                                                         \
			afree(self->data);                                              \
	}                                                                               \
                                                                                        \
	static inline void _ARRAY_METHOD_VARARG(name, push_str,                         \
						const char *str, memmax len)            \
	{                                                                               \
		uint32 required = self->len + len;                                      \
		if (self->cap < required)                                               \
			_CALL_ARRAY_METHOD_VARARG(name, ensure, required);              \
		type *dest = self->data + self->len;                                    \
		memcpy(dest, str, len);                                                 \
		self->len += len;                                                       \
	}

#endif
