#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <stdint.h>
#include <zephyr/kernel.h>

// System health status
typedef enum {
  SYSTEM_HEALTH_GOOD,
  SYSTEM_HEALTH_WARNING,
  SYSTEM_HEALTH_CRITICAL,
  SYSTEM_HEALTH_FAILED
} system_health_t;

// System metrics
typedef struct {
  uint32_t button_events_processed;
  uint32_t lora_messages_sent;
  uint32_t lora_messages_failed;
  uint32_t lora_retries;
  uint32_t led_commands_processed;
  uint32_t system_errors;
  uint64_t uptime_ms;
} system_metrics_t;

// System monitor functions
int system_monitor_init(void);
system_health_t system_monitor_get_health(void);
int system_monitor_get_metrics(system_metrics_t *metrics);
int system_monitor_increment_counter(const char *counter_name);
int system_monitor_log_error(const char *error_msg);

// System monitor thread function
void system_monitor_thread(void *a, void *b, void *c);

// Health check intervals are now in sys_config.h

#endif // SYSTEM_MONITOR_H
