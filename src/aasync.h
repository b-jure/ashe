#ifndef AASYNC_H
#define AASYNC_H

#include "ashe_utils.h"

void mask_signal(int signum, int how);
void setup_default_signal_handling(void);
void disable_async_joblist_update(void);
void enable_async_joblist_update(void);
void sigchld_handler(int signum);
void sigint_handler(__attribute__((unused)) int signum);
void try_wait_missed_sigchld_signals(void);
void unblock_signals(void);
void block_signals(void);

#endif
