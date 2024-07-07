/* ----------------------------------------------------------------------------------------------
 * Copyright (C) 2023-2024 Jure Bagić
 *
 * This file is part of ashe.
 * ashe is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * ashe is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with ashe.
 * If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------------------------*/

#ifndef AARRAY_H
#define AARRAY_H

/* [======== GENERIC ARRAY =========] */

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>

#include "aalloc.h"
#include "acommon.h"
#include "alibc.h"

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
#define ARRAY_NEW(name, type)                                                                    \
	typedef struct {                                                                         \
		a_uint32 cap;                                                                    \
		a_uint32 len;                                                                    \
		type *data;                                                                      \
	} name;                                                                                  \
                                                                                                 \
	static inline void _ARRAY_METHOD(name, init)                                             \
	{                                                                                        \
		self->cap = 0;                                                                   \
		self->len = 0;                                                                   \
		self->data = NULL;                                                               \
	}                                                                                        \
                                                                                                 \
	static void _ARRAY_METHOD(name, grow)                                                    \
	{                                                                                        \
		a_uint32 oldcap = self->cap;                                                     \
		if (a_unlikely(oldcap >= UINT_MAX)) {                                         \
			ashe_panicf("%d:%s: capacity limit of %u reached in array '%s'!",        \
				    __LINE__, __FILE__, #name, UINT_MAX);                        \
		}                                                                                \
		if (oldcap != 0 && !ASHE_ISPOW2(oldcap)) {                                       \
			oldcap |= (oldcap >> 1);                                                 \
			oldcap |= (oldcap >> 2);                                                 \
			oldcap |= (oldcap >> 4);                                                 \
			oldcap |= (oldcap >> 8);                                                 \
			oldcap |= (oldcap >> 16);                                                \
			oldcap++;                                                                \
			oldcap >>= 1;                                                            \
		}                                                                                \
		self->cap = a_max(GROW_ARRAY_CAPACITY(oldcap), UINT_MAX);                     \
		self->data = (type *)ashe_realloc(self->data, self->cap * sizeof(type));         \
	}                                                                                        \
                                                                                                 \
	static inline a_ubyte _ARRAY_METHOD_VARARG(name, ensure, a_uint32 len)                   \
	{                                                                                        \
		a_ubyte grew;                                                                    \
                                                                                                 \
		grew = 0;                                                                        \
		while (self->cap < self->len + len) {                                            \
			grew = 1;                                                                \
			_CALL_ARRAY_METHOD(name, grow);                                          \
		}                                                                                \
		return grew;                                                                     \
	}                                                                                        \
                                                                                                 \
	static inline void _ARRAY_METHOD_VARARG(name, init_cap, a_uint32 cap)                    \
	{                                                                                        \
		_CALL_ARRAY_METHOD(name, init);                                                  \
		_CALL_ARRAY_METHOD_VARARG(name, ensure, cap);                                    \
	}                                                                                        \
                                                                                                 \
	static inline a_uint32 _ARRAY_METHOD_VARARG(name, push, type value)                      \
	{                                                                                        \
		if (self->cap <= self->len)                                                      \
			_CALL_ARRAY_METHOD(name, grow);                                          \
		self->data[self->len++] = value;                                                 \
		return self->len - 1;                                                            \
	}                                                                                        \
                                                                                                 \
	static inline type _ARRAY_METHOD(name, pop)                                              \
	{                                                                                        \
		return self->data[--self->len];                                                  \
	}                                                                                        \
                                                                                                 \
	static inline type *_ARRAY_METHOD_CONST_VARARG(name, index, a_uint32 idx)                \
	{                                                                                        \
		return &self->data[idx];                                                         \
	}                                                                                        \
                                                                                                 \
	static inline type *_ARRAY_METHOD_CONST(name, last)                                      \
	{                                                                                        \
		return &self->data[self->len - 1];                                               \
	}                                                                                        \
                                                                                                 \
	static inline type *_ARRAY_METHOD_CONST(name, first)                                     \
	{                                                                                        \
		return &self->data[0];                                                           \
	}                                                                                        \
                                                                                                 \
	static inline void _ARRAY_METHOD_VARARG(name, insert, a_uint32 index, type value)        \
	{                                                                                        \
		if (index == self->len) {                                                        \
			_CALL_ARRAY_METHOD_VARARG(name, push, value);                            \
			return;                                                                  \
		}                                                                                \
		_CALL_ARRAY_METHOD_VARARG(name, ensure, 1);                                      \
		type *src = self->data + index;                                                  \
		memmove(src + 1, src, (self->len - index) * sizeof(type));                       \
		self->len++;                                                                     \
		self->data[index] = value;                                                       \
	}                                                                                        \
                                                                                                 \
	static inline void _ARRAY_METHOD_VARARG(name, insert_n, a_uint32 index, const type *ptr, \
						a_memmax len)                                    \
	{                                                                                        \
		type *arr;                                                                       \
                                                                                                 \
		_CALL_ARRAY_METHOD_VARARG(name, ensure, len);                                    \
		arr = self->data + index;                                                        \
		memmove(arr + len, arr, (self->len - index) * sizeof(type));                     \
		memmove(arr, ptr, len * sizeof(type));                                           \
		self->len += len;                                                                \
	}                                                                                        \
                                                                                                 \
	static inline type _ARRAY_METHOD_VARARG(name, remove, a_uint32 index)                    \
	{                                                                                        \
		type *dest;                                                                      \
		type retval;                                                                     \
		if (index == self->len)                                                          \
			return _CALL_ARRAY_METHOD(name, pop);                                    \
		dest = self->data + index;                                                       \
		retval = *dest;                                                                  \
		memmove(dest, dest + 1, (self->len - index - 1) * sizeof(type));                 \
		self->len--;                                                                     \
		return retval;                                                                   \
	}                                                                                        \
                                                                                                 \
	static inline void _ARRAY_METHOD_VARARG(name, remove_n, a_uint32 index, a_memmax len)    \
	{                                                                                        \
		type *arr;                                                                       \
                                                                                                 \
		ashe_assert(self->len >= len);                                                   \
		if (a_unlikely(len == 0))                                                     \
			return;                                                                  \
		arr = self->data + index;                                                        \
		memmove(arr, arr + len, (self->len - index - len) * sizeof(type));               \
		self->len -= len;                                                                \
	}                                                                                        \
                                                                                                 \
	static inline void _ARRAY_METHOD_VARARG(name, free, FreeFn fn)                           \
	{                                                                                        \
		a_uint32 i;                                                                      \
                                                                                                 \
		if (fn != NULL)                                                                  \
			for (i = 0; i < self->len; i++)                                          \
				fn((void *)&self->data[i]);                                      \
		if (self->cap > 0) {                                                             \
			ashe_assert(self->data != NULL);                                         \
			ashe_free(self->data);                                                   \
		}                                                                                \
	}                                                                                        \
                                                                                                 \
	static inline void _ARRAY_METHOD_VARARG(name, push_str, const char *str, a_memmax len)   \
	{                                                                                        \
		ashe_assert(sizeof(type) == sizeof(char));                                       \
		_CALL_ARRAY_METHOD_VARARG(name, ensure, len);                                    \
		type *dest = self->data + self->len;                                             \
		memmove(dest, str, len * sizeof(char));                                           \
		self->len += len;                                                                \
	}                                                                                        \
                                                                                                 \
	static inline void _ARRAY_METHOD_VARARG(name, push_ptr, const void *ptr)                 \
	{                                                                                        \
		char temp[ASHE_MAXNUMSTR];                                                      \
		a_ssize chars;                                                                   \
                                                                                                 \
		ashe_assert(sizeof(type) == sizeof(char));                                       \
		chars = ashe_snprintf(temp, sizeof(temp) - 1, ASHE_PTR_FMT, ptr);                \
		_CALL_ARRAY_METHOD_VARARG(name, push_str, temp, chars);                          \
	}                                                                                        \
                                                                                                 \
	static inline void _ARRAY_METHOD_VARARG(name, push_double, double f)                     \
	{                                                                                        \
		char temp[ASHE_MAXNUMSTR];                                                      \
		a_ssize chars;                                                                   \
                                                                                                 \
		ashe_assert(sizeof(type) == sizeof(char));                                       \
		chars = ashe_snprintf(temp, sizeof(temp) - 1, ASHE_DOUBLE_FMT, f);               \
		_CALL_ARRAY_METHOD_VARARG(name, push_str, temp, chars);                          \
	}                                                                                        \
                                                                                                 \
	static inline void _ARRAY_METHOD_VARARG(name, push_number, a_ssize n)                    \
	{                                                                                        \
		char temp[ASHE_MAXNUMSTR];                                                      \
		a_ssize chars;                                                                   \
                                                                                                 \
		ashe_assert(sizeof(type) == sizeof(char));                                       \
		chars = ashe_snprintf(temp, sizeof(temp) - 1, ASHE_NUMBER_FMT, n);               \
		_CALL_ARRAY_METHOD_VARARG(name, push_str, temp, chars);                          \
	}

#endif
