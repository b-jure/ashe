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

#ifndef AASYNC_H
#define AASYNC_H

#include "autils.h"

void ashe_init_sighandlers(void);
void ashe_mask_signal(int signum, int how);
void ashe_mask_signals(a_int32 how);
void ashe_disable_jobcntl_updates(void);
void ashe_enable_jobcntl_updates(void);

#endif
