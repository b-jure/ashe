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

#ifndef APROMPT_H
#define APROMPT_H

#include "acommon.h"
#include "atoken.h"

#define ASHE_USERSTR_MAX ((ARG_MAX >> 2) ? (ARG_MAX >> 2) : 1024)

void ashe_puserstr(const char *str, a_memmax len);
void ashe_pwelcome(void);
void parse_placeholders(a_arr_char *out, const char *str);

#endif
