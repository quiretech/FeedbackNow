---
title: FlexBox firmware — Backend uplink/downlink cheatsheet
author: Jatan J. Pandya
date: 2026-05-14
header-includes:
  - \usepackage{fvextra}
  - \DefineVerbatimEnvironment{Highlighting}{Verbatim}{breaklines,commandchars=\\\{\}}
  - \usepackage{etoolbox}
  - \AtBeginEnvironment{longtable}{\small}
---

# Uplink / downlink schema (reference for backend)

Payload layout, FPorts, event bytes, and application downlinks. Device is **LoRaWAN Class A, OTAA**; application payload length is **11 bytes** (`PAYLOAD_LEN_BYTES` / DR0-style fixed frame).

All uplink timestamps in this document are **UTC epoch seconds, big-endian**, bytes **0–3**, unless noted otherwise.

**Firmware version this doc was aligned with:** see `v1.1/app/include/sys_config.h` (`FW_VERSION_*` / `HW_VERSION_*`). Regenerate or bump the date when you change payloads.

---

## FPorts (uplink routing)

| FPort | Use |
|-------|-----|
| **10** | Button (public vote), event **0x00** |
| **11** | NFC (check-in **0x01**, check-out **0x02**, registered vote **0x03**) |
| **20** | Housekeeping: battery **0x10**, counter sync **0x12**, device state snapshot **0x13** |
| **21** | Device info: firmware/hardware version **0x14** (queued only after downlink **0x08**) |
| **30** | Alarms (reserved; not used on air yet) |
| **13** | Reserved macro `FPORT_FUTURE` in firmware — **no** defined uplinks today |

**Downlink:** Application commands are selected by **first FRMPayload byte** (command). The dispatcher logs `port` but **does not route** on FPort; any application FPort is accepted for these commands.

---

## Uplink confirmation policy (important for RX windows)

Configured in `sys_config.h` (field-stable profile):

| Traffic | Typical `confirmed` | Notes |
|---------|---------------------|--------|
| Button **0x00** | **Unconfirmed** | Avoids RX timeout on weak links |
| NFC **0x01–0x03** | **Confirmed** | ACK expected; use for provisioning / staff flows |
| Housekeeping **0x10**, **0x12**, **0x13** | **Unconfirmed** | Includes periodic heartbeat battery frame |
| Device info **0x14** | **Unconfirmed** | Sent after DL **0x08** |

**Class A rule:** downlink to the device is only possible in **RX1/RX2** following an **uplink** the network accepted. Prefer **confirmed** NFC when you must deliver a downlink in the next receive slot.

---

## Uplink: Event codes and payloads (11 bytes)

Common: `[0..3]` = timestamp (uint32 BE), `[4]` = event code. Remaining bytes are event-specific.

### 0x00 — Button (public vote)

| Field | Bytes | Format |
|-------|-------|--------|
| ts | 0–3 | uint32 BE |
| evt | 4 | 0x00 |
| button_id | 5 | 0..5 |
| counter | 6–8 | 24-bit BE (per-button) |
| reserved | 9–10 | 0 |

- **FPort:** 10  
- **When:** User presses a feedback button.  
- **Backend:** Use counter for idempotency / reconciliation.

---

### 0x01 — NFC check-in

| Field | Bytes | Format |
|-------|-------|--------|
| ts | 0–3 | uint32 BE |
| evt | 4 | 0x01 |
| uid | 5–8 | 4-byte card UID |
| reserved | 9–10 | 0 |

- **FPort:** 11  
- **When:** Staff NFC check-in (staff flow / button 0 context in UI).

---

### 0x02 — NFC check-out

| Field | Bytes | Format |
|-------|-------|--------|
| ts | 0–3 | uint32 BE |
| evt | 4 | 0x02 |
| uid | 5–8 | 4-byte card UID |
| reserved | 9–10 | 0 |

- **FPort:** 11  
- **When:** Staff NFC check-out. Device updates “Last Cleaned” locally for display.

---

### 0x03 — NFC registered vote

| Field | Bytes | Format |
|-------|-------|--------|
| ts | 0–3 | uint32 BE |
| evt | 4 | 0x03 |
| button_id | 5 | 0..5 |
| card_data | 6–9 | 4-byte card UID |
| reserved | 10 | 0 |

- **FPort:** 11  
- **When:** Staff taps NFC then selects a button (registered vote).

---

### 0x10 — Battery status (heartbeat)

| Field | Bytes | Format |
|-------|-------|--------|
| ts | 0–3 | uint32 BE |
| evt | 4 | 0x10 |
| battery_mv | 5–6 | uint16 BE (mV) |
| percent | 7 | 0..100 (**currently 0** in housekeeping path; reserved) |
| flags | 8 | bitfield (0 for now) |
| reserved | 9–10 | 0 |

- **FPort:** 20  
- **When:** Part of **housekeeping** (`housekeeping_run`): periodic daily tick (with DevEUI jitter) and after downlink **0x04** status request.  
- **Backend:** Treat as heartbeat / telemetry; good moment to queue a downlink for the next uplink.

**Heartbeat timing (jitter):** When enabled, first run each UTC day at:

`heartbeat_time_UTC = 00:00 UTC + offset_minutes`

`offset_minutes = (DevEUI[7]×256 + DevEUI[6]) % 1440`

(DevEUI is 8 bytes; indices 6 and 7 are the last two octets.)

---

### 0x12 — Counter sync

| Field | Bytes | Format |
|-------|-------|--------|
| ts | 0–3 | uint32 BE |
| evt | 4 | 0x12 |
| button_id | 5 | 0..5 |
| counter | 6–8 | 24-bit BE |
| reserved | 9–10 | 0 |

- **FPort:** 20  
- **When:** After join and during housekeeping; rate-limited / staggered across buttons.  
- **Backend:** Reconcile per-device, per-button counters.

---

### 0x13 — Device state snapshot (new since 1.2.1 cheatsheet)

| Field | Bytes | Format |
|-------|-------|--------|
| ts | 0–3 | uint32 BE (uplink time; RTC or monotonic fallback) |
| evt | 4 | 0x13 |
| last_cleaned_epoch | 5–8 | uint32 BE (epoch seconds for EPD “Last Cleaned”; 0 if unset) |
| tz_offset_minutes | 9–10 | **int16 BE** (signed minutes to add to UTC for local display; **same encoding as DL 0x03**, negative = west of UTC) |

- **FPort:** 20  
- **When:** Queued after join-related flows, with housekeeping, after DL **0x03** (timezone write succeeds), and as part of the **0x04** status-request path.  
- **Backend:** Single frame for “what the device thinks” about last cleaned + timezone.

---

### 0x14 — Firmware / hardware version + EPD line

**11 bytes.** This event **does not** use the usual leading UTC timestamp (`[0..3]` + `[4]=evt`). Decoders must branch on **FPort 21** and/or `payload[0]==0x14`.

| Field | Bytes | Format |
|-------|-------|--------|
| evt | 0 | `0x14` |
| fw_major | 1 | uint8 |
| fw_minor | 2 | uint8 |
| fw_patch | 3 | uint8 |
| hw_major | 4 | uint8 |
| hw_minor | 5 | uint8 |
| hw_patch | 6 | uint8 |
| **epd_enabled** | **7** | **`0`** = no EPD in build (variant **A**), **`1`** = EPD in build (variant **B**). Mirrors preprocessor **`EPD_ENABLED`** in `sys_config.h`. |
| reserved | 8–10 | `0` |

- **FPort:** **21**  
- **When:** Only after application downlink **0x08** (version query).  
- **Backend:** Read semver from bytes **1–6**; byte **7** selects UI / inventory bucket for display vs headless builds.

---

### 0x11 — Low battery alarm

- Defined in firmware; **not** asserted on the wire in current builds. Same FPort family as housekeeping when implemented.

---

### 0xFF — Reserved

- Reserved for future event types.

---

\newpage

## Downlink: Application commands (FRMPayload)

First byte = **command**. Length must satisfy each command’s minimum (see below).

| Cmd | Code | Min len | Payload shape | Effect |
|-----|------|---------|---------------|--------|
| EPD “Last Cleaned” epoch | **0x01** | 5 | `01` + epoch uint32 BE | Store / display “Last Cleaned” on EPD from epoch. |
| EPD full refresh | **0x02** | 1 | `02` | Full e-paper refresh (reduce ghosting). |
| Timezone offset | **0x03** | 3 | `03` + int16 BE signed minutes (add to UTC for local civil time) | Persist TZ for local “Last Cleaned”; refresh display; queue **0x13** snapshot. |
| Status / housekeeping | **0x04** | 1 | `04` | Runs **housekeeping** path: battery **0x10** (if ADC OK), **LinkCheckReq**, counter-sync burst, **0x13** snapshot, schedules time sync after burst — not “battery only” anymore. |
| Reset counters | **0x05** | 1 | `05` | Reset all button counters (EEPROM); uses power rail for store access. |
| Factory reset | **0x06** | 1 | `06` | Counters + **DevNonce** + **has_joined_once** cleared; LED pattern then **delayed reboot** (first-boot style join). |
| Reboot | **0x07** | 1 | `07` | Request controlled reboot via SMF. |
| FW/HW query | **0x08** | 1 | `08` | Queue uplink **0x14** on **FPort 21** (compact layout: no ts; semver + EPD flag). |
| Custom EPD text | **0x99** | ≥ 2 | `99` + **dur_min** + body | Body = **ASCII hex pairs** (even count); decoded bytes must be printable **0x20–0x7E**. Shows fullscreen banner for **dur_min** minutes; **dur_min = 0** uses `DL_CUSTOM_TEXT_DEFAULT_MINUTES` from `sys_config.h`. Then reverts to normal cleaning / last-cleaned UI. |

Unknown first-byte commands are logged and ignored.

---

\newpage

**Timezone examples (0x03 payload = `03` + int16 BE signed offset minutes):**

| Region | Offset (min) | Hex payload |
|--------|--------------|-------------|
| **New York** (EST, winter) | −300 | `03 FE D4` |
| **New York** (EDT, summer) | −240 | `03 FF 10` |
| **Denver / Mountain** (MST) | −420 | `03 FE 5C` |
| **Denver / Mountain** (MDT) | −360 | `03 FE 98` |
| **San Francisco / Pacific** (PST) | −480 | `03 FE 20` |
| **San Francisco / Pacific** (PDT) | −420 | `03 FE 5C` |

To switch a device: send one downlink with the 3-byte payload above. No reboot required for TZ alone.

**How to send:** Queue application downlink with FRMPayload = command byte + arguments. FPort is not interpreted for command selection (any app port, e.g. 10, is fine). **Class A:** expect delivery in RX1/RX2 after the next accepted uplink.

**EPD update example:** Epoch 1771131600 → `01 69 91 51 90`. `https://www.epochconverter.com/hex`

**Custom text example (0x99):** Message `"Hi"` → ASCII `48 69` → hex digits `4869` as ASCII in payload: `99 05 34 38 36 39` = cmd `99`, dur 5 min, hex body `4869` (pairs: `48`→'H', `69`→'i').

---

## Notes

- **0x04** is the right command when you want a **telemetry burst** and an immediate chance for **downlink** on the subsequent uplinks (battery + counters + **0x13** snapshot, then coordinated time sync).  
- Button IDs **0..5** (six buttons). Counters are **24-bit BE** in button and counter-sync frames.  
- **0x13** and **0x14** are the main **backend-visible** additions since the 2026-02-24 1.2.1 cheatsheet; downlink **0x07**, **0x08**, and **0x99** are new commands.

---

\newpage

## Example payloads (reference hex)

Decoded samples. Bytes **0–3** = UTC epoch (uint32 BE); byte **4** = event code.

| FPort | Base64 | Hex | Explanation |
|:---|:---|:---|:---|
| 10 | `aZ3guwAAAAAEAAA=` | `69 9d e0 bb 00 00 00 00 04 00 00` | **Button (0x00):** ts `0x699DE0BB`, button 0, counter 4. |
| 11 | `aZ3g7wGqqru7AAA=` | `69 9d e0 ef 01 aa aa bb bb 00 00` | **NFC check-in (0x01):** uid `AA AA BB BB`. |
| 11 | `aZ3hCAKqqru7AAA=` | `69 9d e1 08 02 aa aa bb bb 00 00` | **NFC check-out (0x02).** |
| 11 | `aZ3hKQMCqqq7uwA=` | `69 9d e1 29 03 02 aa aa bb bb 00` | **NFC vote (0x03):** button 2, card `AA AA BB BB`. |
| 20 | `aZ3YuRIAAAABAAA=` | `69 9d d8 b9 12 00 00 00 01 00 00` | **Counter sync (0x12):** button 0, counter 1. |
| 20 | `aZ3ZwBAM1wAAAAA=` | `69 9d d9 c0 10 0c d7 00 00 00 00` | **Battery (0x10):** `battery_mv = 0x0CD7` (3287 mV), percent 0, flags 0. |
| 20 | *(construct)* | `68 68 00 00 13 00 00 00 00 00 00` | **Snapshot (0x13):** example ts `0x68680000`, last_cleaned `0`, tz offset `0`. |
| 21 | *(construct)* | `14 01 03 01 01 04 01 01 00 00 00` | **Version (0x14):** FW **1.3.1**, HW **1.4.1**, **epd_enabled=1**; trailing three bytes reserved. |

**Decoding reference:** Most events: `[0..3]` = ts BE, `[4]` = evt. **Exception — 0x14 on FPort 21:** `[0]=0x14`, `[1..3]` fw, `[4..6]` hw, `[7]` EPD flag, `[8..10]=0`.
