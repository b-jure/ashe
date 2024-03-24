#ifndef AASYNC_H
#define AASYNC_H

#include "autils.h"

void init_signal_handlers(void);
void ashe_mask_signals(int32 how);
void disable_async_jobcntl_updates(void);
void enable_async_jobcntl_updates(void);

#endif
