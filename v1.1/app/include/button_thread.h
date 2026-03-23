#ifndef BUTTON_THREAD_H
#define BUTTON_THREAD_H

#include <zephyr/kernel.h>

/* Thread is defined in button_thread.c */
extern const k_tid_t button_uplink_thread_id;
int button_thread_wait_until_ready(k_timeout_t timeout);

#endif /* BUTTON_THREAD_H */
