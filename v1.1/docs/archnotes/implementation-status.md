# FlexBox v1.2 — Implementation Status (vs FRD & Architecture)

**As of:** Post Phase 1 + Phase 2 + combos (SMF, LoRa→SMF, counter-sync, downlink, combo detection, Staff/DeviceInfo/Reboot with timeouts and Staff-first).

---

## Implemented (FRD + Architecture)

### FRD 4.1 Button behavior (Public / Normal only)

| Requirement | Status | Notes |
|-------------|--------|-------|
| Single button tap → feedback vote | Done | Input posts single_button_0..6 to SMF; SMF (Normal) calls `app_logic_public_vote()`. |
| LED solid 1s on acceptance | Done | `led_manager_blink_once(0)` in app_logic. |
| 5s lockout between presses | Done | `BUTTON_COOLDOWN_MS` in app_logic. |
| Uplink Event 0x00 with button ID + counter | Done | `payload_gen_build_button()` → `lora_put_event(FPORT_BUTTON)`. Unconfirmed (FRD 4.5). |
| Button counters 24-bit, persist (EEPROM) | Done | `button_counter_store` with EEPROM slots, CRC, delayed flush. |
| Disconnected: presses accepted, counters persist; on rejoin send 0x07 | Done | Counters always incremented; `lora_put_event` returns -ENOTCONN when not joined; on **joined** SMF runs counter-sync (Event 0x07 per button). |

### FRD 4.4 LED feedback (subset)

| Requirement | Status | Notes |
|-------------|--------|-------|
| Button accepted (public): solid 1s | Done | `led_manager_blink_once`. |
| Join success: visual feedback | Done | LoRa thread: 5× 200ms blink on join. |
| Normal / idle: off | Done | LED off when not in a pattern. |
| **LED UI (RTOS)** | Done | Single LED thread + message queue; callers post commands (OFF/ON/BLINK_ONCE/PATTERN_JOIN_SUCCESS), no blocking or per-LED timers in app code. |
| Other patterns (Staff solid, NFC 1Hz, 3 blinks, etc.) | Not done | Require Staff/NFC/ProcessAction (Phase 3). |

### FRD 4.5 LoRa communication

| Requirement | Status | Notes |
|-------------|--------|-------|
| LoRaWAN Class A, OTAA, unconfirmed uplinks | Done | `lora_thread`, `lorawan_join`, `lorawan_send`. |
| Event 0x00 Button Press (timestamp, button ID, counter) | Done | `payload_gen_build_button`, FPORT_BUTTON. |
| Event 0x07 Counter Sync on rejoin | Done | On `SMF_EVT_JOINED`, SMF runs `smf_do_counter_sync()` (Event 0x07 per button, rate-limited). |
| Downlink: 0x01 EPD update, 0x02 EPD refresh, 0x03 status request, 0x04 reset counters | Done (dispatch only) | `lora_app_dl_callback` → `smf_post_downlink` → SMF `smf_handle_downlink`. 0x01/0x02/0x03 logged or no-op; **0x04** calls `button_counter_store_factory_reset()`. |
| Heartbeat 0x04, Low Battery 0x05, Events 0x01/0x02/0x03 (NFC) | Not done | Payload event types defined; no heartbeat/rejoin timers, no NFC. |

### FRD 4.6 Storage

| Requirement | Status | Notes |
|-------------|--------|-------|
| Button counters in EEPROM, persist across power/battery | Done | `button_counter_store`. |
| DevNonce (replay protection) | Done | `devnonce_store`. |
| No full event log when disconnected; counters only | Done | Only counters in EEPROM; no uplink queue persisted. |

### FRD 4.8 Boot sequence

| Requirement | Status | Notes |
|-------------|--------|-------|
| Init power, EEPROM, RTC, LoRa, buttons; start threads; main does not block on join | Done | Main: init → start LoRa thread, SMF thread, (optional) buttons + Input thread → sleep. No `lora_wait_for_join`. |

### Architecture (Layer 1–3)

| Component | Status | Notes |
|-----------|--------|-------|
| **System mode FSM** | Done (Normal only) | Single SMF thread, single `k_msgq`; states Normal, Staff, NFCScan, DeviceInfo, Reboot, ProcessAction (others stubbed). |
| **Single SMF input queue** | Done | All events (button single, JOINED, DOWNLINK) go through `smf_msgq`. |
| **Input → SMF** | Done (single press only) | Input thread reads `buttons_get_event()`, posts `SMF_EVT_BUTTON_SINGLE_0`..`_6`; no combo events yet. |
| **App logic invoked from SMF** | Done | In Normal, single-button event → `app_logic_public_vote()`; JOINED → counter-sync; DOWNLINK → command dispatch. |
| **LoRa posts joined / downlink to SMF** | Done | `lora_thread`: `smf_post_event(SMF_EVT_JOINED, …)`. `lora_app_dl_callback`: `smf_post_downlink(port, len, data)`. |
| **Counters & storage** | Done | Single owner; app logic calls increment/get; EEPROM, CRC. |
| **RTC / time** | Done | `rtc.c`, `time_sync.c`; timestamps in payloads; time sync after join. |
| **LED** | Done (subset) | `led_manager`: set_led, blink_once; pattern timing in LED layer. |

---

## Not implemented (FRD + Architecture)

### Combo / Staff / NFC (explicitly deferred)

- Button combos (0+1 hold 2s, 0+1+5 hold 3s, 0+1+2 hold 3s, 0+1+2+3 hold 10s).
- Staff mode, NFC Scan, Device Info, Reboot, ProcessAction states and transitions.
- NFC subsystem (PN5180, ISO15693, 4-byte User ID).
- Check-in (0x01), check-out (0x02), registered vote (0x03) uplinks and flows.
- Deliberate join (Staff + 0+1+2) and **has_joined_once** in EEPROM (first boot no auto-join).

### EPD (Variant A)

- E-paper display states (Last Cleaned, Thanks, Cleaning, Device Info, Connecting).
- EPD timers (5s thanks, 30s device info, 45min cleaning auto-revert).
- Downlink 0x01/0x02 applied to EPD (currently no-op).

### LoRa / system behavior

- **Heartbeat:** Daily Event 0x04, DevEUI jitter, battery %, RTC sync.
- **Rejoin:** Hourly when disconnected; post “disconnected” to SMF (optional); rejoin attempts.
- **Low battery:** ADC, threshold, Event 0x05 uplink.
- **Status request (0x03):** Trigger status/heartbeat uplink (currently only logged).

### Power / boot

- Deep sleep (GPIO + RTC wake), light sleep during NFC timeout.
- Power-on LED: 2 quick flashes; join success: 3 quick flashes (we have 5× 200ms).

### Config / alignment

- **NUM_BUTTONS:** 6 (0–5) per FRD; button 6 (gpio0.4) removed from overlay.
- **Counter persist:** FRD “on each button press”; we use delayed flush (5s) as trade-off.

---

## Summary table

| Area | Implemented | Not implemented |
|------|-------------|------------------|
| Public vote (single button, 5s lockout, LED, Event 0x00) | Yes | — |
| Counter-sync on join (Event 0x07) | Yes | — |
| Downlink command dispatch (0x01–0x04) | Yes (0x04 reset; 0x01/0x02/0x03 no-op/log) | EPD actions, status uplink |
| SMF + single queue + LoRa→SMF | Yes | — |
| Combo detection + Staff/DeviceInfo/Reboot (timeouts, Staff-first) | Yes | NFC Scan, ProcessAction |
| has_joined_once / deliberate first join | Yes | join_state_store in EEPROM; first boot waits for Staff+0+1+2, then LoRa join; on success flag persisted for auto-join on next boot. |
| Heartbeat / rejoin timers | — | Yes |
| EPD (Variant A) | — | Yes |
| Low battery Event 0x05 | — | Yes |
| Full LED table (Staff, NFC, etc.) | Partial | Yes |

---

## Testing (combos, timeouts, Staff-first)

**Build and flash:** From `app/`: `west build -b nrf52840dk/nrf52840`, `west flash --runner nrfjprog`. Open a serial console (e.g. 115200 8N1) to see logs. With default log level (INFO) you see mode transitions (e.g. Normal → Staff); for full [Input]/[SMF] per-event traces set `CONFIG_LOG_DEFAULT_LEVEL_DBG` in prj.conf.

**What the framework implements now:**

- **Timeouts:** Staff mode starts a 10s timer; when it expires, SMF gets `STAFF_TIMEOUT` and returns to Normal (LED off). Device Info starts a 30s timer; on expiry SMF returns to Normal.
- **Staff-first:** Join (0+1+2), Reboot (0+1+2+3), and **Device Info (0+1+5)** are only handled when already in Staff. From Normal, those combos are ignored and logged as "enter Staff first".
- **Flow:** Enter Staff with 0+1 hold 2s → LED solid, 20s timeout. While in Staff: hold 0+1+2 for 3s → deliberate join (triggers LoRa join; on first boot device was waiting for this), back to Normal; hold 0+1+2+3 for 10s → Reboot (LED 3s then `sys_reboot`); hold 0+1+5 for 3s → Device Info (30s timeout, LED off), then back to Normal. First boot: no auto-join; log "FIRST BOOT: waiting for deliberate join (Staff + 0+1+2)". After first successful join, has_joined_once is stored in EEPROM; subsequent boots auto-join.

**How to test:**

1. **Single button:** Tap button 0 (or 1–5), release. Expect `[Input] single button N -> SMF`, then `[SMF] state=Normal -> app_logic_public_vote`, LED blink, uplink if joined.
2. **Staff + timeout:** Hold buttons 0+1 for 2s. Expect `[Input] combo fired: ev=...`, `[SMF] Normal -> Staff (LED solid, 10s timeout)`, LED on. Wait 10s. Expect `[SMF] Staff -> Normal (timeout)`, LED off.
3. **Staff-first:** Without entering Staff, hold 0+1+2 for 3s. Expect combo fired then `[SMF] state=Normal -> COMBO_JOIN ignored (enter Staff first)`. Then enter Staff (0+1 hold 2s), then hold 0+1+2 for 3s: expect `[SMF] Staff -> Normal (deliberate join; ...)`.
4. **Device Info (staff-only):** From Normal, hold 0+1+5 for 3s → expect "COMBO_DEVICE_INFO ignored (enter Staff first)". Enter Staff (0+1 hold 2s), then hold 0+1+5 for 3s → expect `[SMF] Staff -> DeviceInfo (30s timeout)`. Wait 30s: expect `[SMF] DeviceInfo -> Normal (timeout)`.
5. **Reboot (careful):** Enter Staff (0+1 hold 2s), then hold 0+1+2+3 for 10s. Expect `[SMF] Staff -> Reboot (LED 3s then reboot)`; after 3s the device reboots.

---

## Production build

Default build is production-oriented: real buttons only (no demo path), single boot path (init → LoRa + SMF + Input threads → main sleeps). Logging: INFO shows mode transitions and errors; per-event/combo details are at DBG. For full `[Input]` / `[SMF]` traces, set `CONFIG_LOG_DEFAULT_LEVEL_DBG` in `prj.conf`. Compile-time flags that affect behavior (e.g. `EEPROM_JOIN_STATE_CLEAR_ON_BOOT`, `EEPROM_*_FACTORY_RESET_ON_BOOT`, `SYS_CONFIG_EEPROM_PROBE_LOG`) are documented in `app/BUILD.md` and `include/sys_config.h`.

---

## Suggested next steps (priority order)

| Priority | Area | What to do | Why |
|----------|------|------------|-----|
| 1 | **Device Info content** | Implement what Device Info mode actually shows/does (e.g. LED pattern, or EPD "Device Info" screen when EPD exists) | Right now it’s a 30s timeout only; no user-visible behavior. |
| 2 | ~~Deliberate join~~ | Done | First-boot join logic + EEPROM has_joined_once implemented. |
| 3 | ~~Reduce/remove debug logs~~ | Done | `[Input]` / `[SMF]` per-event logs moved to LOG_DBG; mode transitions stay at INFO. |
| 4 | **Downlink 0x03 status request** | When SMF receives downlink 0x03, trigger a status/heartbeat-style uplink (Event 0x04 or similar) | Backend can poll device status. |
| 5 | **Heartbeat / rejoin** | Add daily Event 0x04 heartbeat and (e.g. hourly) rejoin when disconnected | FRD compliance and network reliability. |
| 6 | **NFC + ProcessAction** | Implement NFC Scan mode (PN5180, ISO15693), ProcessAction, and check-in/check-out/registered vote uplinks | Full staff workflow. |
| 7 | **EPD (if Variant A)** | Implement EPD states (Last Cleaned, Thanks, Cleaning, Device Info, Connecting) and downlink 0x01/0x02 | Required for display variant. |
