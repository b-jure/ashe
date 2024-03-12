#ifndef AASYNC_H
#define AASYNC_H

#include "autils.h"

int32 init_signal_handlers(void);
void ashe_mask_signals(int32 how);
void disable_async_jobcntl_updates(void);
void enable_async_jobcntl_updates(void);

#endif
