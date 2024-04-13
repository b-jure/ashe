#ifndef AARRAY_H
#define AARRAY_H

/* [======== GENERIC ARRAY =========] */

#include "aalloc.h"

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>

#define GROW_ARRAY_CAPACITY(cap) ((cap) < 8 ? 8 : (cap) * 2)

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
#define _ARRAY_METHOD_CONST(tname, name) _ARRAY_METHOD_NAME(tname, name)(const tname *self)
/* array method vararg declaration with 'self' as const */
#define _ARRAY_METHOD_CONST_VARARG(tname, name, ...) \
	_ARRAY_METHOD_NAME(tname, name)(const tname *self, __VA_ARGS__)

typedef void (*FreeFn)(void *value);

#define a_arrp_cap(arrp) (arrp)->cap
#define a_arr_cap(arr)	 (arr).cap
#define a_arrp_len(arrp) (arrp)->len
#define a_arr_len(arr)	 (arr).len
#define a_arrp_ptr(arrp) (arrp)->data
#define a_arr_ptr(arr)	 (arr).data

/* Create new 'name' array with 'type' elements */
#define ARRAY_NEW(name, type)                                                                      \
	typedef struct {                                                                           \
		a_uint32 cap;                                                                      \
		a_uint32 len;                                                                      \
		type *data;                                                                        \
	} name;                                                                                    \
                                                                                                   \
	static inline void _ARRAY_METHOD(name, init)                                               \
	{                                                                                          \
		self->cap = 0;                                                                     \
		self->len = 0;                                                                     \
		self->data = NULL;                                                                 \
	}                                                                                          \
                                                                                                   \
	static inline void _ARRAY_METHOD_VARARG(name, init_cap, a_uint32 cap)                      \
	{                                                                                          \
		_CALL_ARRAY_METHOD(name, init);                                                    \
		self->data = (type *)ashe_realloc(self->data, cap * sizeof(type));                 \
		self->cap = cap;                                                                   \
	}                                                                                          \
                                                                                                   \
	static void _ARRAY_METHOD(name, grow)                                                      \
	{                                                                                          \
		a_uint32 oldcap = self->cap;                                                       \
		if (ASHE_UNLIKELY(oldcap >= UINT_MAX)) {                                           \
			ashe_panicf("%d:%s: capacity limit of %u reached in array '%s'!",          \
				    __LINE__, __FILE__, #name, UINT_MAX);                          \
		}                                                                                  \
		if (oldcap != 0 && !ASHE_ISPOW2(oldcap)) {                                         \
			oldcap |= (oldcap >> 1);                                                   \
			oldcap |= (oldcap >> 2);                                                   \
			oldcap |= (oldcap >> 4);                                                   \
			oldcap |= (oldcap >> 8);                                                   \
			oldcap |= (oldcap >> 16);                                                  \
			oldcap++;                                                                  \
			oldcap >>= 1;                                                              \
		}                                                                                  \
		self->cap = ASHE_MIN(GROW_ARRAY_CAPACITY(oldcap), UINT_MAX);                       \
		self->data = (type *)ashe_realloc(self->data, self->cap * sizeof(type));           \
	}                                                                                          \
                                                                                                   \
	static inline a_uint32 _ARRAY_METHOD_VARARG(name, push, type value)                        \
	{                                                                                          \
		if (self->cap <= self->len)                                                        \
			_CALL_ARRAY_METHOD(name, grow);                                            \
		self->data[self->len++] = value;                                                   \
		return self->len - 1;                                                              \
	}                                                                                          \
                                                                                                   \
	static inline type _ARRAY_METHOD(name, pop)                                                \
	{                                                                                          \
		return self->data[--self->len];                                                    \
	}                                                                                          \
                                                                                                   \
	static inline type *_ARRAY_METHOD_CONST_VARARG(name, index, a_uint32 idx)                  \
	{                                                                                          \
		return &self->data[idx];                                                           \
	}                                                                                          \
                                                                                                   \
	static inline type *_ARRAY_METHOD_CONST(name, last)                                        \
	{                                                                                          \
		return &self->data[self->len - 1];                                                 \
	}                                                                                          \
                                                                                                   \
	static inline type *_ARRAY_METHOD_CONST(name, first)                                       \
	{                                                                                          \
		return &self->data[0];                                                             \
	}                                                                                          \
                                                                                                   \
	static inline a_ubyte _ARRAY_METHOD_VARARG(name, ensure, a_uint32 len)                     \
	{                                                                                          \
		a_ubyte grew;                                                                      \
                                                                                                   \
		grew = 0;                                                                          \
		while (self->cap < self->len + len) {                                              \
			grew = 1;                                                                  \
			_CALL_ARRAY_METHOD(name, grow);                                            \
		}                                                                                  \
		return grew;                                                                       \
	}                                                                                          \
                                                                                                   \
	static inline void _ARRAY_METHOD_VARARG(name, insert, a_uint32 index, type value)          \
	{                                                                                          \
		if (index == self->len) {                                                          \
			_CALL_ARRAY_METHOD_VARARG(name, push, value);                              \
			return;                                                                    \
		}                                                                                  \
		_CALL_ARRAY_METHOD_VARARG(name, ensure, 1);                                        \
		type *src = self->data + index;                                                    \
		memmove(src + 1, src, (self->len - index) * sizeof(type));                         \
		self->len++;                                                                       \
		self->data[index] = value;                                                         \
	}                                                                                          \
                                                                                                   \
	static inline void _ARRAY_METHOD_VARARG(name, insert_str, a_uint32 index, const char *str, \
						a_memmax len)                                      \
	{                                                                                          \
		type *arr;                                                                         \
                                                                                                   \
		ashe_assert(sizeof(type) == sizeof(char));                                         \
		_CALL_ARRAY_METHOD_VARARG(name, ensure, len);                                      \
		arr = self->data + index;                                                          \
		memmove(arr + len, arr, (self->len - index) * sizeof(type));                       \
		memmove(arr, str, len);                                                            \
		self->len += len;                                                                  \
	}                                                                                          \
                                                                                                   \
	static inline type _ARRAY_METHOD_VARARG(name, remove, a_uint32 index)                      \
	{                                                                                          \
		type *dest;                                                                        \
		type retval;                                                                       \
		if (index == self->len)                                                            \
			return _CALL_ARRAY_METHOD(name, pop);                                      \
		dest = self->data + index;                                                         \
		retval = *dest;                                                                    \
		memmove(dest, dest + 1, (self->len - index - 1) * sizeof(type));                   \
		self->len--;                                                                       \
		return retval;                                                                     \
	}                                                                                          \
                                                                                                   \
	static inline void _ARRAY_METHOD_VARARG(name, remove_str, a_uint32 index, a_memmax len)    \
	{                                                                                          \
		type *arr;                                                                         \
                                                                                                   \
		ashe_assert(sizeof(type) == sizeof(char));                                         \
		ashe_assert(self->len >= len);                                                     \
		if (ASHE_UNLIKELY(len == 0))                                                       \
			return;                                                                    \
		arr = self->data + index;                                                          \
		memmove(arr, arr + len, (self->len - index - len) * sizeof(type));                 \
		self->len -= len;                                                                  \
	}                                                                                          \
                                                                                                   \
	static inline void _ARRAY_METHOD_VARARG(name, free, FreeFn fn)                             \
	{                                                                                          \
		a_uint32 i;                                                                        \
                                                                                                   \
		if (fn != NULL)                                                                    \
			for (i = 0; i < self->len; i++)                                            \
				fn((void *)&self->data[i]);                                        \
		if (self->cap > 0) {                                                               \
			ashe_assert(self->data != NULL);                                           \
			ashe_free(self->data);                                                     \
		}                                                                                  \
	}                                                                                          \
                                                                                                   \
	static inline void _ARRAY_METHOD_VARARG(name, push_str, const char *str, a_memmax len)     \
	{                                                                                          \
		ashe_assert(sizeof(type) == sizeof(char));                                         \
		if (ASHE_UNLIKELY(len <= 0))                                                       \
			return;                                                                    \
		_CALL_ARRAY_METHOD_VARARG(name, ensure, len);                                      \
		type *dest = self->data + self->len;                                               \
		memcpy(dest, str, len * sizeof(char));                                             \
		self->len += len;                                                                  \
	}                                                                                          \
                                                                                                   \
	static inline void _ARRAY_METHOD_VARARG(name, push_ptr, const void *ptr)                   \
	{                                                                                          \
		char temp[ASHE_INT64_DIGITS];                                                      \
		a_ssize chars;                                                                     \
                                                                                                   \
		ashe_assert(sizeof(type) == sizeof(char));                                         \
		chars = snprintf(temp, sizeof(temp), "%p", ptr);                                   \
		if (ASHE_UNLIKELY(chars < 0 || (a_memmax)chars > sizeof(temp)))                    \
			ashe_panic_libcall(snprintf);                                              \
		_CALL_ARRAY_METHOD_VARARG(name, push_str, temp, chars);                            \
	}                                                                                          \
                                                                                                   \
	static inline void _ARRAY_METHOD_VARARG(name, push_num, a_ssize n)                         \
	{                                                                                          \
		char temp[ASHE_INT64_DIGITS];                                                      \
		a_ssize chars;                                                                     \
                                                                                                   \
		ashe_assert(sizeof(type) == sizeof(char));                                         \
		chars = snprintf(temp, sizeof(temp), "%ld", n);                                    \
		if (ASHE_UNLIKELY(chars < 0 || (a_memmax)chars > sizeof(temp)))                    \
			ashe_panic_libcall(snprintf);                                              \
		_CALL_ARRAY_METHOD_VARARG(name, push_str, temp, chars);                            \
	}

#endif
