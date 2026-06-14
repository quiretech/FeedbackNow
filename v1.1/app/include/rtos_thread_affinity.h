/**
 * @file rtos_thread_affinity.h
 * @brief Thread / API ownership (Zephyr). Reference only — not a compile gate.
 *
 * | Module / API family      | Owning thread / context | Other threads must |
 * |--------------------------|-------------------------|--------------------|
 * | lorawan_*                | lora_thread             | Use lora_cmd_put, |
 * |                          |                         | lora_put_event,    |
 * |                          |                         | smf_post_* only.   |
 * | time_sync_* (MAC/RTC)    | lora_thread             | Request via SMF /  |
 * |                          |                         | lora_cmd_put.      |
 * | rail_manager_*           | Any                     | Refcount pairs;    |
 * |                          |                         | SMF documents 3.3A |
 * |                          |                         | for UI flows.      |
 * | EEPROM stores            | Caller with rail 3.3A   | Short critical     |
 * | (devnonce, counters, …)  | held when needed        | sections; LoRa     |
 * |                          |                         | thread holds for   |
 * |                          |                         | join.              |
 * | display_* / LVGL / EPD   | display_work on         | display_show_thanks_ |
 * |                          | system workqueue        | sync() must NOT run |
 * |                          |                         | on sysworkq (dead- |
 * |                          |                         | lock vs display_work)|
 * | app_logic_public_vote    | smf_thread only         | Calls thanks_sync. |
 * | led_manager_*            | led_ui thread           | SMF posts patterns.|
 * | NFC PN5180               | nfc_worker              | SMF starts scans; |
 * |                          |                         | results via        |
 * |                          |                         | smf_post_nfc_result|
 * | smf_post_event /         | Any producer            | Bounded wait on    |
 * | smf_post_downlink        |                         | smf_msgq.          |
 * | SMF state machine         | smf_thread only        | Single consumer of |
 * |                          |                         | smf_msgq.          |
 * | buttons_get_event        | button_uplink (input)  | GPIO ISR only starts|
 * |                          |                         | debounce timers.   |
 * | housekeeping (HK)        | system workqueue       | k_work_delayable   |
 * |                          |                         | schedules daily; DL|
 * |                          |                         | 0x04 via submit.   |
 * | battery_adc_read_mv      | Slow (sleeps)         | Boot + HK; updates |
 * |                          |                         | shared mV cache.   |
 * | battery_adc_last_mv_get  | Any thread             | Device-info EPD;   |
 * | battery_adc_lorawan_*    | Read path / cache      | LoRa DevStatus cb. |
 *
 * Include this header from one .c (e.g. main) if you want the doc in the build
 * unit; it contains no declarations.
 */
#ifndef RTOS_THREAD_AFFINITY_H
#define RTOS_THREAD_AFFINITY_H
#endif
