#ifndef __ASH_ASYNC_H__
#define __ASH_ASYNC_H__

#include "ashe_utils.h"

#ifdef __cplusplus /* For gtest */
#include <atomic>
extern volatile std::atomic_bool sigchld_recv;
extern volatile std::atomic_bool sigint_recv;
#else
#include <stdatomic.h>
extern volatile atomic_bool sigchld_recv;
extern volatile atomic_bool sigint_recv;
#endif

void setup_default_signal_handling(void);
void disable_async_joblist_update(void);
void enable_async_joblist_update(void);
void unblock_sigchld(void);
void block_sigchld(void);
void unblock_sigint(void);
void block_sigint(void);
void sigchld_handler(int signum);
void sigint_handler(__attribute__((unused)) int signum);
void try_wait_missed_sigchld_signals(void);

#endif
