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

#ifndef ABUILTIN_H
#define ABUILTIN_H

#include "acommon.h"
#include "aarray.h"
#include "aparser.h"

enum a_builtin_type {
	TBI_BUILTIN = 0,
	TBI_BG,
	TBI_CD,
	TBI_CLEAR,
	TBI_FG,
	TBI_JOBS,
	TBI_PENV,
	TBI_PWD,
	TBI_RENV,
	TBI_SENV,
	TBI_EXEC,
	TBI_EXIT,
};

a_int32 ashe_runbin(struct a_simple_cmd *scmd, enum a_builtin_type bi);
a_int32 ashe_isbin(const char *command);

#endif
