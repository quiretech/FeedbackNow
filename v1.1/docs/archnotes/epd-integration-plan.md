# EPD Integration Plan (FlexBox v1.2)

This document scopes the display subsystem, defines how it fits the current architecture, and outlines a modular GUI approach for LVGL so screens and fields are easy to maintain and extend.

---

## 1. Requirements & Goals (Display Subsystem)

### 1.1 From FRD / Architecture

| Requirement | Source | Notes |
|-------------|--------|--------|
| **Display states** | FRD 4.3, Arch Phase 4 | Last Cleaned (default), Thanks, Cleaning, Device Info, Connecting, First boot (Ready) |
| **Triggers** | FRD 3.5, 4.3 | Public vote → Thanks 5s → default; Check-in → Cleaning; Check-out → Last Cleaned + timestamp; 0+1+5 → Device Info 30s; Join → Connecting |
| **Timers** | FRD 3.4, 4.3 | Thanks: 5s then default; Device Info: 30s then Normal; Cleaning: 45min auto-revert to Last Cleaned (current RTC) |
| **Downlink** | FRD 4.5, SMF | 0x01 = update **display store** only (payload: 4-byte epoch big-endian → yyyy/mm/dd hh:mm); 0x02 = full refresh. 0x01 does **not** change RTC. RTC remains gold (LNS/time sync) for uplinks and for NFC check-out. |
| **Variant** | FRD, Arch 4.4 | Single codebase; `CONFIG_EPD_ENABLED=y` (Variant A) vs `n` (Variant B). Variant B: EPD calls no-op, downlink 0x01/0x02 ignored |
| **Non-blocking** | Arch | EPD refresh is slow (seconds); must not block SMF/buttons. Use work queue or dedicated thread |

### 1.2 Display States (Summary)

| State ID | Content | Entry | Exit / Timer |
|----------|---------|--------|--------------|
| **LOGO** | "FeedBackNow FlexBox" (text for now; later C-array SVG) | Boot | First-time device: stays until first join. Already-joined device: when join starts → CONNECTING. After join success → LAST_CLEANED. |
| **LAST_CLEANED** | Heading "Last Cleaned" + timestamp (yyyy/mm/dd hh:mm) from **canonical store** | Boot (after logo, if has_joined_once), Check-out, Thanks timeout, Cleaning 45min timeout, downlink 0x01 applied, join success | — (default) |
| **THANKS** | "Thanks for your Feedback!" | Public button press | 5s → LAST_CLEANED (read timestamp from store) |
| **CLEANING** | "Cleaning in progress" | NFC Check-in (button 0) | Check-out → LAST_CLEANED; or 45min → LAST_CLEANED (current RTC written to store, then show). Safeguard only; no cancel on Staff timeout. |
| **DEVICE_INFO** | DevEUI, FW, battery %, counters (optional; implement last) | Staff + 0+1+5 hold 3s | 30s → Normal |
| **CONNECTING** | "Connecting..." | LoRa join started (device that has joined before) | Join success → LAST_CLEANED; join fail → stay or retry |

### 1.3 Goals for the Display Subsystem

1. **Single owner of “what’s on screen”**  
   One logical state (e.g. LAST_CLEANED, THANKS) at a time; transitions driven by SMF/app/downlink/timers.

2. **Slow work off main path**  
   All EPD refresh (full/partial) runs in a work queue (or low-priority thread) so SMF and button/NFC handling stay responsive.

3. **Rails**  
   EPD is on 3.3A (or same rail as other peripherals). Request 3.3A before any EPD update, release after refresh completes (or after a short keep-alive for multi-step updates).

4. **Timers owned by display or SMF**  
   - Thanks 5s: display subsystem (or SMF posts event after 5s).  
   - Device Info 30s: already SMF (MODE_DEVICE_INFO timeout).  
   - Cleaning 45min: display subsystem (or a dedicated timer that posts “cleaning timeout” to SMF); on expiry show Last Cleaned with current RTC.

5. **Downlink 0x01/0x02**  
   - **0x01**: Payload = 4-byte epoch (big-endian), e.g. `0x01 0x68 0xFB 0xA6 0xE4` → parse to 2025/10/23 09:27 (yyyy/mm/dd hh:mm). **Updates only the EEPROM display store** (what the EPD shows); does **not** change the device RTC. RTC stays the gold standard (LNS sync) for all uplink timestamps and for NFC check-out (we read RTC on check-out and write that value to the display store). Set pending; apply after Thanks+5s when pending, else immediately (write to store, then show LAST_CLEANED).  
   - **0x02**: Enqueue full refresh (clear ghosting).

6. **Variant B**  
   When `CONFIG_EPD_ENABLED=n`, all “show screen” / “refresh” calls are no-ops; downlink 0x01/0x02 are ignored in command dispatch.

### 1.4 Last Cleaned: Display Store (EEPROM) vs RTC

**RTC = gold standard:** The device RTC (updated via LNS / DeviceTimeReq) is the single source of truth for **all uplink timestamps** and for **NFC check-out**. When NFC check-out happens we **read the RTC** and use that value for the uplink and for the display (write RTC to the display store).

**Display store (EEPROM):** One EEPROM region holds the timestamp **shown on the EPD** as "last cleaned". It is **not** used to set or correct the RTC.

- **Writers:** (1) NFC check-out → read **RTC** → write RTC value to display store (and send uplink with RTC). (2) Downlink 0x01 → when applied (after Thanks+5s or immediately), write **payload epoch** to display store only (backend can set what the display shows; RTC unchanged).
- **Readers:** Whenever we show LAST_CLEANED, **read from display store** and format as yyyy/mm/dd hh:mm.
- **Boot / first use:** If store has never been written, write **current RTC** once for "visual symmetry".
- **Optional RAM "pending":** Downlink 0x01 can set a pending timestamp in RAM; "apply" = write pending → EEPROM display store, clear pending, show LAST_CLEANED.

**EEPROM layout:** Add `EEPROM_LAST_CLEANED_OFF` and `EEPROM_LAST_CLEANED_SIZE` in `sys_config.h`; keep non-overlapping with counters, DevNonce, join state.

---

## 2. Architecture: How EPD Fits the Current System

### 2.1 Layering

- **SMF / app_logic** (existing): Decide *when* to show which screen (e.g. on public vote → Thanks; on NFC result check-out → Last Cleaned + timestamp; on Device Info entry → Device Info screen).
- **Display subsystem (new)**: Owns EPD state machine (which screen is active), enqueues “show screen X with data Y” and “full refresh” onto a **display work queue**. Runs all SSD1683/LVGL and refresh in the work queue so the rest of the system is non-blocking.

### 2.2 Data Flow

- **SMF / app_logic** → call e.g. `display_show(EPD_SCREEN_THANKS)` or `display_show_last_cleaned(epoch_s)` or `display_show_device_info(...)`.  
- **Downlink handler** (SMF): on 0x01 set pending Last Cleaned (and optionally timestamp); on 0x02 enqueue full refresh.  
- **Display subsystem**:  
  - Maintains current screen + pending “Last Cleaned” timestamp (if any).  
  - Work thread/queue: take next job (show screen / full refresh), request 3.3A, render (LVGL or direct framebuffer), push to SSD1683, refresh EPD, release 3.3A.  
  - Internal timers: Thanks 5s (then show Last Cleaned; if pending downlink 0x01, use that timestamp); Cleaning 45min (then show Last Cleaned with current RTC).

### 2.3 Where Hooks Go (Existing Code)

| Event | Current handler | EPD hook |
|-------|------------------|----------|
| Public vote | `app_logic_public_vote()` | After LED, call `display_show(EPD_SCREEN_THANKS)`; display starts 5s timer. |
| NFC check-in | SMF MODE_NFC_SCAN on result, intent CHECK_IN | Call `display_show(EPD_SCREEN_CLEANING)`; display starts 45min timer (or cancel previous). |
| NFC check-out | SMF MODE_NFC_SCAN on result, intent CHECK_OUT | Get RTC timestamp; call `display_show_last_cleaned(epoch_s)`; cancel 45min timer. |
| NFC vote (2–5) | SMF MODE_NFC_SCAN on result, intent VOTE | No EPD change per FRD. |
| Device Info entry | SMF MODE_DEVICE_INFO entry | Call `display_show(EPD_SCREEN_DEVICE_INFO)` with DevEUI, version, battery, counters. |
| Device Info exit | SMF MODE_DEVICE_INFO timeout | Optionally call `display_show(EPD_SCREEN_LAST_CLEANED)` with stored timestamp (no change to “current” Last Cleaned value). |
| Join started | LoRa thread / SMF | Call `display_show(EPD_SCREEN_CONNECTING)`. |
| Join success / fail | SMF (JOINED) or similar | Call `display_show(EPD_SCREEN_LAST_CLEANED)` or keep READY on first boot. |
| Boot | main / init | Show **logo** ("FeedBackNow FlexBox"). First-time device (never joined): stays on logo until first join. Already-joined: when LoRa join fires → CONNECTING → on success LAST_CLEANED (read from store). At boot, if store empty, write current RTC to store for visual symmetry. |
| Downlink 0x01 | `smf_handle_downlink` | Parse 4-byte epoch (big-endian); set pending. Apply after Thanks+5s when pending, else apply now: write to last-cleaned store, show LAST_CLEANED. |
| Downlink 0x02 | `smf_handle_downlink` | Enqueue full refresh job. |

### 2.4 CONFIG_EPD_ENABLED

- When `CONFIG_EPD_ENABLED=n`:  
  - `display_show*` and refresh APIs are empty inlines or no-op functions.  
  - Downlink 0x01/0x02 in `smf_handle_downlink` are not applied (no-op).  
- When `y`:  
  - Display subsystem and work queue are compiled in; SSD1683/LVGL used as configured.

---

## 3. LVGL and Modular GUI Maintenance

### 3.1 High-Level Approach

- **Screen = one logical view** (Logo, Last Cleaned, Thanks, Cleaning, Device Info, Connecting).  
- **Rendering**: LVGL builds the framebuffer for that screen; then we push the framebuffer to the SSD1683 (via Zephyr display API or direct driver).  
- **Static vs dynamic**:  
  - **Static**: Labels that don’t change per state (e.g. “LAST CLEANED AT:”, “Thanks for your Feedback!”, “Cleaning in progress”, “Connecting…”, “Ready”).  
  - **Dynamic**: Fields that change (timestamp, DevEUI, FW version, battery %, counters).  
  - We keep “screen templates” (which widgets and where) and only **update the dynamic fields** when showing that screen, so we can change layout/copy in one place without touching state logic.

### 3.2 Suggested Module Layout (for when you add LVGL)

- **`display_manager`** (or `epd_manager`)  
  - Owns: current screen enum, pending Last Cleaned timestamp, 5s/45min timers.  
  - API: `display_show(screen_id)`, `display_show_last_cleaned(epoch_s)`, `display_show_device_info(...)`, `display_set_pending_last_cleaned(epoch_s)`, `display_request_full_refresh()`.  
  - Internally: enqueue job for work queue; work queue runs “render this screen + refresh”.

- **`display_screens`** (or `gui_screens`)  
  - One function (or one small struct) per screen: e.g. `screen_logo_render()`, `screen_last_cleaned_render(timestamp)`, `screen_thanks_render()`, `screen_cleaning_render()`, `screen_connecting_render()`, `screen_device_info_render(...)` (optional, last).  
  - Each “render” function:  
    - Sets up the **static** layout (labels, positions) if not already set, or uses a pre-built LVGL “template” object.  
    - Fills in **dynamic** fields (timestamp, DevEUI, etc.).  
  - This keeps “what the screen looks like” in one place per screen, so you can tweak text/position without touching state machine or timers.

- **Static content**  
  - Define constants or small tables for strings that don’t change: e.g. “LAST CLEANED AT:”, “Thanks for your Feedback!”, “Cleaning in progress”, “Connecting…”, “Ready”, “Device Info”, “DevEUI:”, “FW:”, “Battery:”, “Counters:”.  
  - Layout (positions, font sizes) can live in the same file as the screen render function or in a shared `display_theme.h` / `gui_config.h`.

- **Dynamic content**  
  - Last Cleaned: single timestamp string (from RTC or downlink).  
  - Device Info: DevEUI string, FW version string, battery % number, counter values.  
  - These are passed into the render function or set via small setters so the same screen can be “refreshed” with new data (e.g. 45min revert with “now”) without redefining the whole screen.

### 3.3 Default States and Easy Updates

- **Boot**:  
  - Show a **boot logo** (single image or LVGL screen) once, then transition to either READY (first boot) or LAST_CLEANED (stored timestamp from last check-out or RTC).  
  - “Default” after boot = LAST_CLEANED with stored timestamp; if none, use RTC now.

- **Easy UI updates**  
  - Change “stock” content by editing only the screen render functions and static strings.  
  - Add a new screen: add an enum value, a render function, and a case in the work queue that calls that render function.  
  - Timers (5s, 45min) and “when to show what” stay in `display_manager` and SMF; layout and copy stay in `display_screens` / theme.

### 3.4 Order of Implementation (Suggestion)

1. **Phase 3.a (your step)**  
   - Add LVGL and get one test screen (e.g. “Hello” or boot logo) rendering to the EPD via the existing SSD1683 + Zephyr display API.  
   - Confirm dimensions (400×300), rotation, and refresh path (work queue, 3.3A request/release).
   - **Reference:** `app/src/display_starter_code.c` — LVGL init, `DT_CHOSEN(zephyr_display)`, fonts (roboto_28, roboto_36, roboto_bold_42), last-cleaned-style layout (heading, lines, timestamp/date labels), and EPD flush pattern (3.3A rail + lv_task_handler). Use as starting points; not all snippets are complete.

2. **Phase 3.b (this plan)**  
   - Introduce `display_manager` with screen enum and `display_show*` API; all EPD work in a single work queue; request/release 3.3A in the work handler.  
   - Implement **screen modules** per state (Last Cleaned, Thanks, Cleaning, Device Info, Connecting, Ready, Boot logo) with clear static vs dynamic split.  
   - Wire SMF/app_logic and downlink to `display_show*` and pending Last Cleaned; implement 5s and 45min timers in display subsystem (or via SMF events).  
   - Apply downlink 0x01 (after Thanks+5s when pending) and 0x02 (full refresh) as above.

---

## 4. Resolved Decisions (Summary)

| Topic | Decision |
|-------|----------|
| **Last Cleaned store** | Single EEPROM region (gold source). Written by: NFC check-out (RTC), downlink 0x01 when applied. Display always reads from store to show LAST_CLEANED. Boot: if store empty, write current RTC once for visual symmetry. |
| **Downlink 0x01 schema** | Payload: 4-byte epoch big-endian → yyyy/mm/dd hh:mm. Updates **display store** only (what EPD shows); RTC stays gold for uplinks and NFC check-out. |
| **Device Info screen** | Optional / nice-to-have; implement last after core EPD and store. |
| **Boot logo** | "FeedBackNow FlexBox" text for now; eventually C-array SVG rendered on EPD. |
| **45min timer** | Start on check-in. No cancel on Staff timeout. If staff takes >45min, screen auto-reverts to Last Cleaned (current RTC written to store); when they check out, screen updates to true value. Safeguard for "forgot to check out". |
| **First boot / Ready** | No separate Ready screen. Logo = boot screen. First-time device stays on logo until first join; then Connecting → Last Cleaned. Already-joined device: logo → (when join fires) Connecting → Last Cleaned. |

*(Previous open points — resolved above.)*

- **Stored “Last Cleaned” timestamp**: Do we persist the last check-out time in EEPROM so after power cycle we show it on boot (and where in EEPROM / format), or do we show “—” / RTC now until first check-out?
- **Device Info** (was open point 2): Exact fields and format (e.g. “DevEUI: XX…”, “FW: 1.2.3”, “Battery: 87%”, “B0: 12 B1: 3 …”) — any layout mock or FRD snippet to match?
3. **Boot logo**: Asset (image file) or LVGL-drawn “FlexBox” / logo text only? One full-screen image or small logo + text?
4. **45min cleaning timer**: Should this timer be cancelled when leaving Staff (e.g. staff times out without checking out), or only when check-out or 45min occurs?
5. **First boot “Ready”**: Should “Ready” stay until first *successful* join, or until first join *attempt* (then show “Connecting…” and then Last Cleaned on success)?

Once these are decided, the same plan can be turned into concrete tasks (files, APIs, and SMF/downlink patches).
