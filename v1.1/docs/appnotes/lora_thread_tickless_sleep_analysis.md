# LoRa Thread Tickless Sleep Analysis & Conversation Record

**Date:** 2026-10-19  
**Document Target:** `FeedbackNow v1.1 / v1.2 Firmware`  
**File Under Review:** [`v1.1/app/src/lora_thread.c`](file:///home/jpandya/Desktop/FBN/fbn_antigravity/FeedbackNow/v1.1/app/src/lora_thread.c#L444-L453)  
**Hardware Platform:** Nordic nRF52840 + Semtech SX1262 (`FLEXBOX` / `FLEXBOX_PLUS` / `FLEXBOX_PLUS_MED`)

---

## 1. Problem Statement

During the initial boot OTAA join backoff sequence in [`lora_thread.c`](file:///home/jpandya/Desktop/FBN/fbn_antigravity/FeedbackNow/v1.1/app/src/lora_thread.c#L444-L453), the processor fails to enter deep tickless sleep during the 1-hour backoff wait:

```c
  while (!run_join_cycle(&join_cfg, show_join_led_boot, cmd == LORA_CMD_JOIN)) {
    (void)smf_post_event(SMF_EVT_JOIN_CYCLE_FAILED, 0, k_uptime_get());
    LOG_WRN("join retry in %d h", (int)LORA_JOIN_BACKOFF_HOURS);
    join_led_show_active(show_join_led_boot);
    if (LORA_JOIN_BACKOFF_HOURS > 0) {
      k_sleep(K_HOURS(LORA_JOIN_BACKOFF_HOURS));
    } else {
      k_sleep(K_MINUTES(1)); /* 0 hours = test: 1 minute */
    }
  }
```

Instead of entering low-power System ON idle sleep (< 10 µA), current consumption remains elevated in the milliamp range (~5–15 mA) and the CPU is periodically interrupted.

---

## 2. Root Cause Analysis

### Cause 1: Active Periodic LED Blinking (`led_ui_thread` Firing Every 800 ms)
- Line 447 calls [`join_led_show_active(show_join_led_boot)`](file:///home/jpandya/Desktop/FBN/fbn_antigravity/FeedbackNow/v1.1/app/src/lora_thread.c#L267-L271) right before entering `k_sleep(...)`.
- When `show_join_led_boot` is `true` (standard on boot when provisioned or when initiated via staff combo), this requests pattern [`LED_PATTERN_JOINING`](file:///home/jpandya/Desktop/FBN/fbn_antigravity/FeedbackNow/v1.1/app/src/led_manager.c#L170-L174).
- In [`led_manager.c`](file:///home/jpandya/Desktop/FBN/fbn_antigravity/FeedbackNow/v1.1/app/src/led_manager.c#L225-L236), `LED_PATTERN_JOINING` toggles every 800 ms:
  ```c
  #define LED_JOINING_ON_MS  800
  #define LED_JOINING_OFF_MS 800
  ```
- In `led_ui_thread_fn`:
  ```c
  k_timeout_t timeout = K_MSEC((int32_t)(delta > 2147483647 ? 2147483647 : delta));
  ret = k_msgq_get(&led_ui_msgq, &msg, timeout);
  ```
- **Result:** The `led_ui_thread` calculates a timeout of 800 ms. The Zephyr kernel timer interrupt repeatedly wakes the processor from idle every 800 ms to toggle the LED GPIO. The processor is blocked from remaining in deep tickless sleep.

### Cause 2: 3.3A Power Rail Kept Energized
- At the start of [`run_join_cycle()`](file:///home/jpandya/Desktop/FBN/fbn_antigravity/FeedbackNow/v1.1/app/src/lora_thread.c#L288), [`join_installer_rail_hold(show_join_led)`](file:///home/jpandya/Desktop/FBN/fbn_antigravity/FeedbackNow/v1.1/app/src/lora_thread.c#L63-L69) requests the 3.3A power rail via [`rail_manager_request_3v3a()`](file:///home/jpandya/Desktop/FBN/fbn_antigravity/FeedbackNow/v1.1/app/src/rail_manager.c#L146).
- When all attempts in a cycle fail, [`run_join_cycle()`](file:///home/jpandya/Desktop/FBN/fbn_antigravity/FeedbackNow/v1.1/app/src/lora_thread.c#L393-L398) exits without calling [`join_installer_rail_release()`](file:///home/jpandya/Desktop/FBN/fbn_antigravity/FeedbackNow/v1.1/app/src/lora_thread.c#L71-L77):
  ```c
  /* Keep JOINING blink + 3.3A through inter-cycle backoff; release on success. */
  join_led_show_active(show_join_led);
  return false;
  ```
- **Result:** `join_installer_rail_held` remains `true` and the reference counts for `ref_3v3a` and `ref_3v3` never return to 0. Both the 3.3A and 3.3V power rails remain powered throughout the entire 1-hour backoff period, keeping regulators, display, radio, and LED circuits energized.

### Cause 3: SX1262 Transceiver Retained in Standby Mode
- Because the 3.3A rail is kept on and Zephyr's LoRaWAN subsystem does not issue a sleep command to the transceiver on join failure, the SX1262 remains in Standby mode (~600 µA to 1.2 mA).

### Cause 4: `show_join_led_boot` is Immutable (`const bool`)
- Line 442 declares `const bool show_join_led_boot = join_installer_led_for_cmd(cmd);`.
- On subsequent iterations of the while loop (after each 1-hour backoff), `show_join_led_boot` is still `true`.
- **Result:** Every automated rejoin attempt re-arms the LED and holds the 3.3A rail, violating the architectural rule:
  > *"silent only for first cycle after boot auto-post (installer at power-on), never for backoff rejoin."* (lines 79–80)

---

## 3. Resolution

To allow the processor to enter tickless sleep during the 1-hour backoff:
1. **Turn off the LED pattern:** Call [`led_manager_show(0, LED_PATTERN_OFF)`](file:///home/jpandya/Desktop/FBN/fbn_antigravity/FeedbackNow/v1.1/app/include/led_manager.h) so `led_ui_thread` has `next_ms = 0` and sleeps indefinitely (`K_FOREVER`).
2. **Release the 3.3A rail:** Call [`join_installer_rail_release()`](file:///home/jpandya/Desktop/FBN/fbn_antigravity/FeedbackNow/v1.1/app/src/lora_thread.c#L71-L77) so external power rails drop to 0 V.
3. **Make `show_join_led` mutable:** Clear it to `false` after the first cycle so all subsequent backoff rejoin attempts run silently.

### Proposed Code Diff

```diff
--- a/v1.1/app/src/lora_thread.c
+++ b/v1.1/app/src/lora_thread.c
@@ -441,12 +441,17 @@ static void lora_thread_fn(void *a, void *b, void *c) {
   LOG_STATE("join cmd=%u starting cycle", cmd);
 
-  const bool show_join_led_boot = join_installer_led_for_cmd(cmd);
+  bool show_join_led = join_installer_led_for_cmd(cmd);
 
-  while (!run_join_cycle(&join_cfg, show_join_led_boot, cmd == LORA_CMD_JOIN)) {
+  while (!run_join_cycle(&join_cfg, show_join_led, cmd == LORA_CMD_JOIN)) {
     (void)smf_post_event(SMF_EVT_JOIN_CYCLE_FAILED, 0, k_uptime_get());
     LOG_WRN("join retry in %d h", (int)LORA_JOIN_BACKOFF_HOURS);
-    join_led_show_active(show_join_led_boot);
+
+    /* Allow tickless sleep during backoff: turn off LED and release 3.3A rail */
+    (void)led_manager_show(0, LED_PATTERN_OFF);
+    join_installer_rail_release();
+    show_join_led = false; /* Subsequent backoff retries must be silent */
+
     if (LORA_JOIN_BACKOFF_HOURS > 0) {
       k_sleep(K_HOURS(LORA_JOIN_BACKOFF_HOURS));
     } else {
```

---

## 4. Expected Behavior After Fix

1. **Active Cycle:** During active OTAA attempts, 3.3A is requested and the LED blinks (if armed for the first boot or deliberate staff join).
2. **Failure Entry:** When join attempts fail, the LED is commanded `LED_PATTERN_OFF`, setting `next_ms = 0`. The `led_ui_thread` blocks on `k_msgq_get(..., K_FOREVER)`.
3. **Power Down:** `join_installer_rail_release()` drops the rail refcount. The 3.3A rail regulator turns off.
4. **Tickless Sleep:** With all worker threads waiting on semaphores/queues with `K_FOREVER`, only the `lora_thread` has a pending timeout (`K_HOURS(1)`). Zephyr's RTC1 timer handles the duration with no periodic software wakes, achieving low-power sleep (< 10 µA).
