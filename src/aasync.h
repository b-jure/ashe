#ifndef AASYNC_H
#define AASYNC_H

#include "autils.h"

void ashe_init_sighandlers(void);
void ashe_mask_signal(int signum, int how);
void ashe_mask_signals(int32 how);
void ashe_disable_jobcntl_updates(void);
void ashe_enable_jobcntl_updates(void);

#endif
