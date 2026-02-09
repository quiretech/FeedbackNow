# FlexBox v1.2 — Zephyr Architecture (Living Document)

**Revision:** [Rev 1 - 2026-02-08]  
**FRD:** [fb-now-v1.2.md](../appnotes/fb-now-v1.2.md)  
**Target:** nRF52840, Zephyr RTOS — 6 buttons, NFC (PN5180), LoRa (SX1262), optional EPD, RTC, EEPROM, 1 LED, battery.

---

## Document Conventions

| Convention | Meaning |
|------------|--------|
| **Revisions** | Mark with `[Rev N - date]` or "Superseded by ..." so old ideas stay visible but clearly deprecated. |
| **No deletion** | Do not delete superseded content; strike-through or move to "Previous / deprecated" subsection. |
| **Reasoning** | Each major decision gets a short "Why" or "Trade-off" bullet. |
| **Open questions** | Collected in a dedicated subsection; resolved items moved to "Decisions" with rationale. |
| **Diagrams** | Mermaid only (no spaces in node IDs; double-quote labels with special chars). |

---

## Session Workflow

```mermaid
flowchart LR
  L1[Layer1_System] --> P1[Pause]
  P1 --> L2[Layer2_Concurrency]
  L2 --> P2[Pause]
  P2 --> L3[Layer3_RTOS]
  L3 --> P3[Pause]
  P3 --> L4[Layer4_StateMachines]
  L4 --> P4[Pause]
  P4 --> L5[Layer5_EventModel]
  L5 --> Done[DocComplete]
```

After each layer: confirm or refine; then update this doc and proceed.

---

## Layer 1: System-Level Architecture

### Subsystems / Components

| Subsystem | Responsibility | Owns / Drives |
|-----------|----------------|---------------|
| **Input** | Button scanning (0–5), combo detection (0+1, 0+1+2, 0+1+5, 0+1+2+3), hold timers, debounce | Raw button state; combo events (e.g. staff_mode_request, device_info_request) |
| **System mode** | Normal / Staff / NFC Scan / Device Info / Reboot path; timeouts (10s staff, 5s NFC, 30s device info) | Current system mode; mode transition logic |
| **NFC** | PN5180 control, ISO15693 read, 4-byte User ID; only active when mode = NFC Scan | NFC session state; success/failure/timeout result |
| **LoRa / network** | SX1262, LoRaWAN Class A, OTAA, join/rejoin, uplink scheduling, downlink handling | Network state (joined/disconnected); uplink queue when connected; downlink command handling |
| **Application logic** | Map (mode + button + NFC) to actions: public vote, check-in, check-out, registered vote; enforce public 5s lockout; trigger uplink payload build | Action requests; no persistent event queue when disconnected (counters only per FRD) |
| **Counters & storage** | 6× 24-bit button counters, EEPROM read/write, CRC, DevNonce, network and first-join flags | Counter values; persistence policy (e.g. write on each button press) |
| **RTC / time** | External RTC, timestamps for uplinks and EPD; DeviceTimeReq on join; daily heartbeat jitter (DevEUI-based) | Current time; alarm for wake from deep sleep |
| **LED** | Single LED; patterns: off, solid, 1 Hz blink, 3 fast blinks, etc., per FRD table | LED state machine (pattern + duration) |
| **EPD** (Variant A) | E-paper states: default "Last Cleaned", thanks, cleaning, device info, connecting; 5s/30s/45min timers; downlink update after thanks+5s | Display content state; refresh triggers |
| **Power** | Battery ADC, low-battery threshold → Event 0x05; deep sleep (GPIO + RTC wake), light sleep during NFC timeout | Power state; sleep policy |

### State Ownership (High-Level)

- **System mode**: owned by one place (system mode / state manager) so timeouts and transitions are deterministic.
- **Network state**: owned by LoRa/network subsystem; others observe (e.g. for counter-sync on rejoin).
- **Counters**: owned by counters & storage; application logic requests increment and persist; no other subsystem mutates counters.
- **LED/EPD**: each owns its own output state; they are consumers of "what to show" (driven by mode and application events).

### Interfaces (Conceptual, No Code)

- **Input → System mode**: "Button combo detected" (event or call); system mode applies timeouts and transitions.
- **System mode → NFC**: "Start NFC scan" / "Cancel NFC scan"; NFC → System mode: "NFC result" (success + User ID, timeout, error).
- **Application logic ↔ LoRa**: "Request uplink" (event type + payload); LoRa reports "joined" / "disconnected" / "downlink received".
- **Application logic ↔ Counters**: "Increment(button_id)"; "Get counter(button_id)" for payloads.
- **Application logic → LED / EPD**: "Show pattern X" / "Show screen Y"; LED/EPD handle timing locally.
- **RTC**: "Get timestamp"; "Set alarm for heartbeat / rejoin"; "Wake from sleep".
- **Power**: "Enter deep sleep" / "Enter light sleep"; battery sample on heartbeat.

### Trade-offs and Pitfalls (Layer 1)

- **Single owner for system mode**: Avoids two threads both trying to transition (timeout vs button) and race conditions; timeouts and button combos must be evaluated in one logical place or serialized.
- **No event queue when disconnected**: FRD says only counters in EEPROM when disconnected; counter-sync (0x07) on rejoin reconciles. Application logic does not maintain a persistent uplink queue in disconnected mode.
- **EPD and LED timing**: Long EPD updates (e.g. 5s thanks) and LED patterns must not block button or NFC handling; EPD/LED subsystems own internal timers.
- **Variant A vs B**: One codebase with `CONFIG_EPD_ENABLED`; EPD subsystem is optional; downlink EPD commands no-op in Variant B.

### Resolved Questions (Decisions)

- **"Queue Flush" on reconnect (FRD 4.9):** Upon joining, the device sends events for each button that has counters to uplink (Counter Sync, Event 0x07). So "Queue Flush" = run counter-sync uplinks per button, not a literal queued-event flush. *Why:* No full event queue is stored when disconnected; only counters persist; backend reconciles from last known count vs post-rejoin count.
- **First boot vs has_joined_once:** Deliberate join (Staff + 0+1+2) before any auto-join. Once the device has joined successfully, we store a flag in EEPROM (`has_joined_once`) so that subsequent boots can perform automatic join retries (10 attempts on boot, then hourly). System-mode and network subsystems both rely on this flag from storage.

---

## Layer 2: Concurrency Model

### What Runs Concurrently

| Execution context | Role | Blocking / non-blocking |
|-------------------|------|--------------------------|
| **Main / system-mode thread** | Owns system mode FSM; evaluates button combos and timeouts; drives mode transitions and coordinates NFC start/cancel | Can block on short waits (e.g. debounce); must not block on long I/O (NFC, EEPROM) if done in same thread — delegate or use work. |
| **Button / input** | Scan GPIO, detect combos and hold durations | Typically timer-driven or thread with short sleep; debounce is short; combo timers (2s, 3s, 10s) need a single place to avoid races. |
| **LoRa stack** | MAC/PHY, join, send, receive; Class A RX windows | Blocking on radio is acceptable in a dedicated thread or in LoRa driver callbacks; application must not assume immediate send. |
| **NFC** | PN5180 poll/read when in NFC Scan mode | Blocking with timeout (e.g. 5s) is acceptable in NFC thread or work queue; must be cancellable when mode exits. |
| **LED** | Run pattern (solid, blink, N blinks) | Non-blocking from app perspective: LED owns a timer/thread that advances pattern; "set pattern" is a request. |
| **EPD** | Update display; run 5s/30s/45min timers | EPD refresh can block (slow); run in work queue or low-priority thread so main loop and buttons remain responsive. |
| **RTC / alarms** | Heartbeat jitter, rejoin interval, wake from sleep | Timer callbacks or alarm IRQ; minimal work in ISR; schedule work for heartbeat/rejoin logic. |
| **Power / sleep** | Deep sleep (GPIO + RTC wake), light sleep during NFC timeout | Decided by main/supervisor; only one "sleep decision" owner. |

### Event-Driven vs Polling

- **Event-driven:** Button combo detected → system mode; NFC result → system mode; join success / disconnect → application + counter-sync; downlink received → command handler; RTC alarm → heartbeat or rejoin.
- **Polling (minimal):** Button GPIO can be polled in a loop with debounce, or via GPIO callback + timers for hold detection; prefer events from Input to System mode rather than System mode polling raw GPIO.

### Blocking Considerations

- **EEPROM write** on each button press: Keep write short; consider small delay or work queue so one slow write does not stall button response (or accept brief block if write is fast enough).
- **NFC read**: Up to 5s timeout; run in dedicated thread or work queue so system mode can still react to timeouts/cancel.
- **LoRa send**: Class A uplink blocks until TX and RX windows complete; LoRa thread or async API so application does not block.
- **EPD update**: Slow (seconds); always offloaded so UI and buttons stay responsive.

### Trade-offs (Layer 2)

- **Single thread for system mode + input:** Simplifies ownership of combo and timeout state; risk is one long operation blocking mode transitions — hence delegate NFC, EEPROM, EPD to other contexts.
- **LoRa in its own thread:** Matches common Zephyr LoRa app pattern; allows blocking on radio while rest of system runs.

---

## Layer 3: RTOS Primitive Mapping

### Suggested Mapping (Zephyr)

| Subsystem / concern | Primitive | Rationale |
|---------------------|-----------|-----------|
| **System mode FSM** | SMF (State Machine Framework) or explicit switch/table | SMF gives clear states and transitions; fits Normal/Staff/NFCScan/DeviceInfo/Reboot. Single SMF instance owned by one thread. |
| **Events between subsystems** | zbus (or k_msgq / k_poll) | zbus: pub/sub, decouples producers (input, NFC, LoRa, RTC) from consumers (system mode, app logic, LED, EPD). Alternative: message queues if you prefer explicit readers. |
| **Button scanning + combo** | Thread + k_timer for hold detection, or work queue triggered by GPIO callback | Thread allows periodic scan and combo state machine; timers for 2s/3s/10s holds. |
| **LoRa** | Dedicated thread (or LoRa stack thread) | Stack often has its own thread; app thread submits uplink requests via zbus or queue. |
| **NFC** | Work queue (k_work) or dedicated thread | When system mode enters NFC Scan, submit work to run PN5180 read with timeout; work can be cancelled on mode exit. |
| **LED** | Thread or k_timer in low-priority thread | Timer-driven state machine for pattern (on/off/blink count); "set pattern" via zbus or API. |
| **EPD** | Work queue | "Show screen X" enqueues work; work runs to completion (blocking EPD is OK here). |
| **Heartbeat / rejoin** | k_timer or RTC alarm | Timer callback posts event (e.g. zbus) to trigger heartbeat or rejoin logic in app/main thread. |
| **Counters & storage** | Service called from app logic (same or other thread); mutex if shared | EEPROM access serialized; increment + persist can be synchronous API. |

### Why These Choices

- **SMF for system mode:** Clear state names and transitions; easy to map to FRD table; one place for timeouts (entry timers or SMF + timer).
- **zbus for events:** Reduces coupling; Input, NFC, LoRa, RTC publish; system mode and application logic subscribe. Optional: use only where it adds value (e.g. mode transitions, uplink requests, downlink received).
- **Work for NFC and EPD:** Keeps long or blocking work off the main FSM thread; work can be cancelled (NFC) or serialized (EPD).
- **Dedicated LoRa thread:** Aligns with Zephyr LoRa sample design; radio and MAC block in that thread.

### Open / Alternative

- If zbus is not used: k_msgq or k_poll for a few channel types (combo, nfc_result, uplink_request, downlink_received, rtc_alarm) achieve similar decoupling with explicit queues.

---

## Layer 4: State Machines

### System Mode FSM (FRD-Aligned)

```mermaid
stateDiagram-v2
    [*] --> NormalMode
    NormalMode --> StaffMode: Btn_0_1_hold_2s
    NormalMode --> DeviceInfo: Btn_0_1_5_hold_3s
    StaffMode --> NormalMode: Timeout_10s
    StaffMode --> NormalMode: Deliberate_join_0_1_2_hold_3s
    StaffMode --> Reboot: Btn_0_1_2_3_hold_10s
    StaffMode --> NFCScan: Btn_0_to_5_pressed
    DeviceInfo --> NormalMode: Any_btn_or_timeout_30s
    Reboot --> [*]: LED_solid_3s_then_reboot
    NFCScan --> NormalMode: Timeout_5s_or_fail
    NFCScan --> ProcessAction: Card_read_ok
    ProcessAction --> NormalMode: Done
```

- **Normal:** Public taps (0–5) → vote + lockout 5s; no staff combo active.
- **Staff:** LED solid; 10s timeout or combo (join/reboot) or single button → NFC Scan.
- **NFC Scan:** LED 1 Hz blink; 5s timeout or fail → Normal; success → ProcessAction then Normal.
- **Device Info:** EPD (Variant A) or status uplink; 30s or any button → Normal.
- **Reboot:** LED solid 3s then reboot.
- **ProcessAction:** Check-in / check-out / registered vote; then transition to Normal.

### Timeouts — Who Triggers

| Timeout | Value | Owner | Mechanism |
|---------|--------|--------|-----------|
| Staff mode | 10s | System mode FSM | k_timer started on entry to Staff; on expiry post event or call FSM "timeout"; FSM transitions to Normal. |
| NFC scan | 5s | System mode or NFC | NFC work with 5s timeout; on timeout NFC posts "nfc_timeout"; FSM transitions to Normal. |
| Device info | 30s | System mode FSM | k_timer on entry to DeviceInfo; on expiry transition to Normal. |
| Public lockout | 5s | Application logic | Last-vote timestamp; reject press if &lt; 5s. |
| EPD "Thanks" | 5s | EPD subsystem | Internal timer; after 5s EPD returns to "Last Cleaned". |
| Cleaning auto-revert | 45min | EPD (Variant A) | Timer on check-in; on expiry set "Last Cleaned" with current RTC. |

### Optional Sub–State Machines

- **Network state:** Disconnected → Joining → Joined; used by LoRa subsystem; application observes for counter-sync on Joined.
- **LED pattern:** Idle / Solid / Blink_1Hz / N_fast_blinks; LED subsystem; entry/exit driven by events from system mode and application logic.
- **EPD screen:** Default / Thanks / Cleaning / DeviceInfo / Connecting; EPD subsystem; transitions on application and downlink events plus internal timers.

---

## Layer 5: Event Model

### Event Types (Conceptual)

| Event | Publisher | Subscribers | Notes |
|-------|-----------|-------------|--------|
| **button_combo** | Input | System mode | Payload: combo id (staff_request, device_info, deliberate_join, reboot, single_button_0..5). |
| **nfc_result** | NFC | System mode, Application logic | Success (User ID) / timeout / error. |
| **uplink_request** | Application logic | LoRa | Event type (0x00–0x07) + payload; LoRa enqueues or sends when connected. |
| **joined** / **disconnected** | LoRa | Application logic, System mode (if needed) | On joined: trigger counter-sync (Event 0x07 per button). |
| **downlink_received** | LoRa | Application logic (command dispatcher) | Payload: command code + data; dispatch to EPD update, refresh, status request, reset counters. |
| **heartbeat_tick** | RTC / timer | Application logic | Trigger heartbeat uplink (0x04), battery sample, RTC sync. |
| **rejoin_tick** | RTC / timer | LoRa | When disconnected; trigger join attempt (rate-limited). |
| **show_led** | System mode, Application logic | LED | Pattern id + optional duration. |
| **show_epd** | Application logic, Command handler | EPD | Screen id + optional data (timestamp, device info). |

### Sync vs Async

- **Async (post and return):** button_combo, nfc_result, uplink_request, downlink_received, show_led, show_epd — producers do not wait for consumer completion.
- **Sync (optional):** Increment counter + persist could be synchronous call from application logic to Counters & storage so that "next uplink" sees updated value; acceptable if EEPROM write is fast enough.

### zbus as Inspiration

- Channels: e.g. `button_combo`, `nfc_result`, `uplink_request`, `network_status`, `downlink`, `led_cmd`, `epd_cmd`, `heartbeat_tick`, `rejoin_tick`.
- Subscribers: system mode (button_combo, nfc_result); application logic (nfc_result, network_status, downlink); LoRa (uplink_request); LED (led_cmd); EPD (epd_cmd); heartbeat/rejoin from timer to app/LoRa.
- If not using zbus: same events can be implemented with k_msgq or k_poll for each channel type.

### Summary Diagram

```mermaid
flowchart TB
  Input -->|button_combo| SystemMode
  NFC -->|nfc_result| SystemMode
  NFC -->|nfc_result| AppLogic
  SystemMode -->|show_led| LED
  SystemMode -->|show_epd| EPD
  AppLogic -->|uplink_request| LoRa
  AppLogic -->|show_led| LED
  AppLogic -->|show_epd| EPD
  AppLogic -->|increment/get| Counters
  LoRa -->|joined/disconnected| AppLogic
  LoRa -->|downlink_received| AppLogic
  RTC -->|heartbeat_tick| AppLogic
  RTC -->|rejoin_tick| LoRa
```

---

## Gap Analysis: v1.1/app vs Architecture

**Codebase:** [v1.1/app](../../app) — LoRa + EEPROM + buttons + LED (no NFC, no EPD).

### What Already Matches

| Area | Current implementation | Architecture alignment |
|------|------------------------|------------------------|
| **Counters & storage** | `button_counter_store`: 6 (or 7) buttons, EEPROM slots, CRC, mutex, increment/get, delayed flush | Matches: single owner, app requests increment; FRD says 6 buttons and persist on press — current flush is 5s delayed (trade-off: less EEPROM wear vs strict “on each press” persist). |
| **Button payload** | `payload_gen_build_button(button_id, epoch_s, ...)` builds timestamp + event type + button_id + 24-bit counter; uses `button_counter_store_inc` | Matches: application logic builds uplink payload; counters owned by store. |
| **LoRa thread** | Dedicated `lora_thread`: join loop, then message loop; `lora_msgq` for uplinks; `lora_put_event` / `lora_get_event` | Matches: LoRa in its own thread; uplink requests via queue. |
| **LED** | `led_manager`: init, set_led, blink_once (1s); k_timer + k_work for blink-off | Matches: LED owns pattern timing; callers use “set pattern” style API. |
| **RTC** | `rtc.c`, `time_sync.c`; timestamps for payloads; time sync after join | Matches: RTC for timestamps; time sync on join. |
| **Public vote flow** | Button thread: get event → debounce + 5s cooldown → LED blink → build button payload → `lora_put_event` | Matches “Normal mode public vote” slice: lockout 5s, LED feedback, uplink. No system mode yet — effectively “always Normal”. |
| **Downlink** | `lora_app_dl_callback` exists; only logs payload and time-updated flag | Partial: mechanism present; no command dispatch (EPD update, refresh, status, reset counters) yet. |

### What Is Missing

| Gap | Architecture expectation | Current state |
|-----|-------------------------|---------------|
| **System mode FSM (SMF)** | Single SMF as orchestrator; states: Normal, Staff, NFC Scan, Device Info, Reboot, ProcessAction | Absent. No mode; code behaves as “always Normal”. |
| **Single SMF input queue** | One `k_msgq` (or k_poll) feeding the SMF thread: button_combo, nfc_result, timeouts, joined, downlink, heartbeat, rejoin | Absent. Button events go to `button_msgq` consumed by button thread; LoRa does not post “joined”/“downlink” to any orchestrator queue. |
| **Input → combo detection** | Input subsystem detects combos (0+1 hold 2s, 0+1+5 hold 3s, 0+1+2 hold 3s, 0+1+2+3 hold 10s) and single presses; posts to SMF queue | Missing. Buttons: raw press/release only; no hold timers, no combo detection; events go to button thread, not to an SMF queue. |
| **has_joined_once / first boot** | First boot: no auto-join; deliberate join (Staff + 0+1+2). After first join, store flag in EEPROM; subsequent boots: 10 attempts then hourly retry | Missing. LoRa thread joins on every boot until success; main blocks on `lora_wait_for_join(120s)`. No EEPROM flag, no deliberate-join flow. |
| **Counter-sync on join** | On “joined”, run counter-sync (Event 0x07 per button) and post to SMF or run in app context | Missing. No counter-sync after join. |
| **Disconnected operation** | When not joined: accept button presses, update counters, no uplink queue; on rejoin do counter-sync | Partial. `lora_put_event` returns -ENOTCONN when not joined (drops message); counters still incremented. No rejoin timer, no counter-sync on rejoin. |
| **Heartbeat / rejoin timers** | Daily heartbeat (DevEUI jitter); hourly rejoin when disconnected | Missing. No heartbeat or rejoin scheduling. |
| **NFC** | NFC subsystem; only active in NFC Scan; posts nfc_result to SMF queue | Not present (out of scope in current app). |
| **EPD** | EPD subsystem (Variant A); show screen, timers | Not present (out of scope in current app). |

### Conflicts / Divergence

| Issue | Architecture | Current code | Suggested direction |
|-------|--------------|-------------|--------------------|
| **Who consumes button events** | Input posts **combo** or **single press** to **SMF input queue**; SMF thread runs FSM and calls app logic for vote (in Normal). | Button thread consumes **raw** press from `button_msgq` and does vote + uplink itself. | Introduce SMF thread and single input queue; move “vote + uplink” into app logic **called from SMF** when in Normal and message is single_button_0..5. Input layer should either (a) run in a thread that detects combos and single press and posts to SMF queue, or (b) SMF thread polls/reads from a dedicated “input event” queue that a separate input thread fills (combo or single press). |
| **Main thread role** | Main either starts threads and then SMF thread blocks on input queue (no join wait in main), or main is the SMF thread. | Main does init, starts LoRa + button threads, **blocks on lora_wait_for_join(120s)** then sleeps forever. | Remove blocking join from main. Let LoRa thread run; on join it posts “joined” to SMF input queue (when that exists). SMF or app logic reacts (e.g. counter-sync). Main’s only role: init, start SMF thread (and others); SMF thread blocks on queue. |
| **Number of buttons** | FRD: 6 buttons (0–5). | `NUM_BUTTONS` = 7 in sys_config; button_id_map maps 0..6. | Align to 6 buttons (0–5) for FRD; adjust DT/aliases and config. |
| **Counter persist policy** | FRD: “Counters written to EEPROM on each button press.” | button_counter_store uses delayed flush (5s) to reduce wear. | Either keep delayed flush as a documented trade-off or add immediate write on each press (more EEPROM wear). |

### Summary

- **Reuse as-is:** button_counter_store, payload_gen (button payload), led_manager, lora_thread + lora_msgq (concept), RTC/time_sync, power_ctrl.
- **Refactor to align:** (1) Add system-mode SMF and **one** SMF input queue. (2) Input: add combo + hold detection and post to SMF queue (not directly to button thread). (3) Button thread’s “vote + uplink” logic becomes app logic **invoked by SMF** when in Normal and event is single button. (4) LoRa: post “joined” (and optionally “downlink”) to SMF queue; remove main’s blocking join; add counter-sync on join and optional rejoin/heartbeat timers. (5) Add has_joined_once in EEPROM and first-boot deliberate-join policy when Staff/NFC exist.
- **Add later:** NFC subsystem, EPD subsystem (Variant A), downlink command dispatch, heartbeat/rejoin scheduling.

---

## Implementation Roadmap

Phases are ordered so the codebase aligns with the architecture (SMF-only orchestration, single SMF input queue) and is ready to add NFC and EPD with minimal rework.

```mermaid
flowchart LR
  P1[Phase1_SMF_and_Input] --> P2[Phase2_LoRa_and_App]
  P2 --> P3[Phase3_NFC]
  P3 --> P4[Phase4_EPD]
```

### Phase 1: SMF + Input (foundation)

**Goal:** One system-mode FSM (SMF) and one input queue; no NFC/EPD yet; “Normal only” is fine.

| Step | Task | Notes |
|------|------|--------|
| 1.1 | Define SMF states (Normal, Staff, NFCScan, DeviceInfo, Reboot, ProcessAction) and transitions (per Layer 4). | Use Zephyr SMF or explicit switch/table; single SMF instance. |
| 1.2 | Add **SMF thread**: blocks on a single `k_msgq` (SMF input queue). Message type: enum (e.g. button_combo, staff_timeout, nfc_result, joined, downlink, …) + payload union. | Only button_combo and timeouts needed in Phase 1; nfc_result/joined/downlink in later phases. |
| 1.3 | **Input layer:** Either extend existing button driver or add a small “input” thread that reads from `button_msgq`, runs **combo detection** (0+1 hold 2s, 0+1+5 hold 3s, 0+1+2 hold 3s, 0+1+2+3 hold 10s) and **single press** (0..5). Post to **SMF input queue** (combo id or single_button_0..5). | Reuse buttons.c GPIO/interrupt and queue; add hold timers and combo state machine in input thread. |
| 1.4 | SMF thread: on message, run SMF; in **Normal**, if message is single_button_0..5, call **app logic** for public vote (increment counter, LED, build payload, request uplink). | Moves current button_thread vote logic into “app logic” called from SMF. |
| 1.5 | Remove or repurpose **button_thread**: no longer the consumer of button events; SMF thread is. Button thread can be removed once Input posts to SMF queue and SMF invokes app logic. | |
| 1.6 | **Main:** Stop blocking on `lora_wait_for_join`. Main only: init hardware, start LoRa thread, start Input thread (if separate), start **SMF thread**; then main can sleep or exit. | |

**Exit criterion:** Device runs with “Normal only”; single button press → SMF receives single_button event → app logic runs → LED + uplink. Staff/DeviceInfo/Reboot transitions can be stubbed (e.g. no-op or log) until Phase 2/3.

### Phase 2: LoRa and app logic alignment

**Goal:** Join policy (has_joined_once, first-boot no auto-join when we have Staff), “joined” and “downlink” feed SMF; counter-sync on join.

| Step | Task | Notes |
|------|------|--------|
| 2.1 | **has_joined_once in EEPROM:** Add a small region (or reuse existing EEPROM layout) to store “has_joined_once”. Read on boot. First boot (flag clear): do not auto-join; wait for deliberate join (Staff + 0+1+2) in Phase 3. After first successful join: set flag, persist. | LoRa thread or main can read flag; join loop runs only if has_joined_once or “deliberate join” triggered. |
| 2.2 | **LoRa thread posts “joined” to SMF queue** after successful join (and optionally “disconnected” on loss). SMF or app logic (same thread) reacts to “joined”: trigger **counter-sync** (Event 0x07 per button, rate-limited). | Counter-sync: for each button 0..5, build payload with current counter, lora_put_event (when connected). |
| 2.3 | **Downlink:** In `lora_app_dl_callback`, post “downlink_received” (port + payload) to SMF input queue. In SMF or app logic, **command dispatcher**: 0x01 EPD update, 0x02 EPD refresh, 0x03 status request, 0x04 reset counters. | EPD commands no-op until Phase 4; status and reset counters can be implemented now. |
| 2.4 | **Rejoin/heartbeat (optional in Phase 2):** When disconnected, LoRa thread can post “disconnected”; a timer or LoRa thread triggers rejoin every 1h. Heartbeat: daily timer posts “heartbeat_tick” to SMF queue; app logic sends 0x04 heartbeat uplink. | Can be deferred to a later sub-phase. |

**Exit criterion:** Join and disconnect feed SMF; counter-sync runs on join; downlink posts to SMF and command dispatch runs (at least for non-EPD commands).

### Phase 3: NFC and Staff mode

**Goal:** Staff mode entry (0+1 hold 2s), timeouts (10s staff, 5s NFC), NFC scan (work queue), check-in/check-out/registered vote.

| Step | Task | Notes |
|------|------|--------|
| 3.1 | **Staff mode in SMF:** On staff_mode_request (0+1 hold 2s), transition to Staff; start 10s timeout; LED solid. On staff_timeout or deliberate_join (0+1+2 hold 3s), transition to Normal. On 0+1+2+3 hold 10s, transition to Reboot. On single button 0..5 in Staff, transition to NFCScan, pass button id. | |
| 3.2 | **NFC subsystem:** PN5180 driver, ISO15693 read, 4-byte User ID. When SMF enters NFCScan, call NFC_scan_start(button_id); NFC runs in work queue with 5s timeout; on completion posts **nfc_result** (success + User ID, or timeout/error) to SMF queue. | SMF consumes nfc_result; on success transition to ProcessAction with button_id + User ID; on timeout/error to Normal. |
| 3.3 | **ProcessAction:** Check-in (btn 0), check-out (btn 1), registered vote (btn 2..5). Build uplink (0x01/0x02/0x03), LED feedback (3 blinks), then transition to Normal. | |
| 3.4 | **Deliberate join (first boot):** When has_joined_once is false, Staff + 0+1+2 hold 3s triggers join attempt (signal LoRa thread or post “start_join” to LoRa); on success set has_joined_once. | |

**Exit criterion:** Staff mode, NFC scan, check-in/out/registered vote work; first join is deliberate when flag is clear.

### Phase 4: EPD (Variant A)

**Goal:** EPD display states (Last Cleaned, Thanks, Cleaning, Device Info, Connecting); 5s/30s/45min timers; downlink EPD update; CONFIG_EPD_ENABLED.

| Step | Task | Notes |
|------|------|--------|
| 4.1 | **EPD driver/module:** Init, show screen (default, thanks, cleaning, device_info, connecting). Work queue for refresh. | |
| 4.2 | **SMF/app → EPD:** On public vote: show “Thanks” for 5s then default. On check-in: “Cleaning in progress”; on check-out: “Last Cleaned” + timestamp. On Device Info entry: show device info screen; 30s timeout. Cleaning auto-revert: 45min timer to “Last Cleaned” with current RTC. | |
| 4.3 | **Downlink EPD:** Command 0x01 (update Last Cleaned), 0x02 (full refresh). Apply 0x01 after “Thanks” + 5s gap when pending. | |
| 4.4 | **Variant B:** CONFIG_EPD_ENABLED=n: EPD calls no-op; downlink EPD commands ignored. | |

**Exit criterion:** Variant A: all EPD states and timers; Variant B: builds and runs without EPD.

---

## Document History

| Rev | Date | Changes |
|-----|------|--------|
| 1 | 2026-02-08 | Initial architecture: Layers 1–5 (system, concurrency, RTOS primitives, state machines, event model). Resolved: Queue Flush = counter-sync on join; has_joined_once in EEPROM for subsequent retries. |
| 2 | 2026-02-08 | Added **Gap Analysis (v1.1/app vs architecture)** and **Implementation Roadmap** (Phases 1–4: SMF+Input, LoRa+app, NFC, EPD). No code changes. |


// my notes:
make staff uplinks "CONFIRMED SENDS"
