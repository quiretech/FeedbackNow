#include "system_monitor.h"
#include "sys_config.h"

#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(system_monitor, LOG_LEVEL_INF);

// System metrics
static system_metrics_t metrics = {0};
static struct k_mutex metrics_mutex;
static system_health_t current_health = SYSTEM_HEALTH_GOOD;
static int64_t start_time_ms;

int system_monitor_init(void) {
  k_mutex_init(&metrics_mutex);
  start_time_ms = k_uptime_get();

  // Initialize metrics
  memset(&metrics, 0, sizeof(metrics));

  LOG_INF("System monitor initialized");
  return 0;
}

system_health_t system_monitor_get_health(void) { return current_health; }

int system_monitor_get_metrics(system_metrics_t *out_metrics) {
  if (out_metrics == NULL) {
    return -EINVAL;
  }

  k_mutex_lock(&metrics_mutex, K_FOREVER);
  *out_metrics = metrics;
  k_mutex_unlock(&metrics_mutex);

  return 0;
}

int system_monitor_increment_counter(const char *counter_name) {
  if (counter_name == NULL) {
    return -EINVAL;
  }

  k_mutex_lock(&metrics_mutex, K_FOREVER);

  if (strcmp(counter_name, "button_events_processed") == 0) {
    metrics.button_events_processed++;
  } else if (strcmp(counter_name, "lora_messages_sent") == 0) {
    metrics.lora_messages_sent++;
  } else if (strcmp(counter_name, "lora_messages_failed") == 0) {
    metrics.lora_messages_failed++;
  } else if (strcmp(counter_name, "lora_retries") == 0) {
    metrics.lora_retries++;
  } else if (strcmp(counter_name, "led_commands_processed") == 0) {
    metrics.led_commands_processed++;
  } else if (strcmp(counter_name, "system_errors") == 0) {
    metrics.system_errors++;
  } else {
    LOG_WRN("Unknown counter: %s", counter_name);
    k_mutex_unlock(&metrics_mutex);
    return -EINVAL;
  }

  k_mutex_unlock(&metrics_mutex);
  return 0;
}

int system_monitor_log_error(const char *error_msg) {
  if (error_msg == NULL) {
    return -EINVAL;
  }

  LOG_ERR("System error: %s", error_msg);

  k_mutex_lock(&metrics_mutex, K_FOREVER);
  metrics.system_errors++;
  k_mutex_unlock(&metrics_mutex);

  // Update health status
  if (current_health == SYSTEM_HEALTH_GOOD) {
    current_health = SYSTEM_HEALTH_WARNING;
  } else if (current_health == SYSTEM_HEALTH_WARNING) {
    current_health = SYSTEM_HEALTH_CRITICAL;
  }

  return 0;
}

static void system_monitor_update_health(void) {
  k_mutex_lock(&metrics_mutex, K_FOREVER);

  // Calculate health based on metrics
  if (metrics.system_errors > 10) {
    current_health = SYSTEM_HEALTH_CRITICAL;
  } else if (metrics.system_errors > 5) {
    current_health = SYSTEM_HEALTH_WARNING;
  } else {
    current_health = SYSTEM_HEALTH_GOOD;
  }

  // Update uptime
  metrics.uptime_ms = k_uptime_get() - start_time_ms;

  k_mutex_unlock(&metrics_mutex);
}

static void system_monitor_log_metrics(void) {
  system_metrics_t current_metrics;

  if (system_monitor_get_metrics(&current_metrics) == 0) {
    LOG_INF("System Metrics:");
    LOG_INF("  Button events: %u", current_metrics.button_events_processed);
    LOG_INF("  LoRa sent: %u", current_metrics.lora_messages_sent);
    LOG_INF("  LoRa failed: %u", current_metrics.lora_messages_failed);
    LOG_INF("  LoRa retries: %u", current_metrics.lora_retries);
    LOG_INF("  LED commands: %u", current_metrics.led_commands_processed);
    LOG_INF("  System errors: %u", current_metrics.system_errors);
    LOG_INF("  Uptime: %llu ms", current_metrics.uptime_ms);
    LOG_INF("  Health: %d", current_health);
  }
}

void system_monitor_thread(void *a, void *b, void *c) {
  LOG_INF("System monitor thread started");

  while (1) {
    // Update health status
    system_monitor_update_health();

    // Log metrics periodically
    system_monitor_log_metrics();

    // Sleep for the configured interval
    k_sleep(K_MSEC(METRICS_UPDATE_INTERVAL_MS));
  }
}

K_THREAD_DEFINE(system_monitor_thread_id, SYSTEM_MONITOR_THREAD_STACK_SIZE,
                system_monitor_thread, NULL, NULL, NULL,
                SYSTEM_MONITOR_THREAD_PRIORITY, 0, 0);
