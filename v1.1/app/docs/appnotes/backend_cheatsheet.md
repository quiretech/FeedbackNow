---
title: FlexBox 1.2.1 Backend Cheatsheet
author: Jatan J. Pandya
date: 2026-02-24
header-includes:
  - \usepackage{fvextra}
  - \DefineVerbatimEnvironment{Highlighting}{Verbatim}{breaklines,commandchars=\\\{\}}
  - \usepackage{etoolbox}
  - \AtBeginEnvironment{longtable}{\small}
---

# FlexBox 1.2.1 Uplink/Downlink Schema

Reference for backend; Payload layout, FPorts, and downlink commands. 
Device is LoRaWAN Class A, OTAA; max payload **11 bytes**. 
All uplink timestamps are **UTC epoch seconds, big-endian**, bytes 0–3.

---

## FPorts (uplink)

| FPort | Use |
|-------|-----|
| 10 | Button (public vote) |
| 11 | NFC (check-in, check-out, registered vote) |
| 20 | Housekeeping (battery, counter-sync) |
| 30 | Alarms (reserved) |
| 13 | Future |

**Downlink:** Device uses **first byte = command**; FPort is not used for routing. Any FPort is accepted.

---

\newpage

## Uplink: Event codes and payloads

Common: `[0..3]` = timestamp (uint32 BE), `[4]` = event code. Rest = event-specific. All 11 bytes.

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
- **Example:** `69 38 6F 33 00 02 00 01 E2 00 00` (ts≈1765306163, button 2, counter 123874)  
- **Backend:** Uses counter for idempotency/reconciliation.

---

### 0x01 — NFC check-in

| Field | Bytes | Format |
|-------|-------|--------|
| ts | 0–3 | uint32 BE |
| evt | 4 | 0x01 |
| uid | 5–8 | 4-byte card UID |
| reserved | 9–10 | 0 |

- **FPort:** 11  
- **When:** Staff NFC check-in (button 0).  
- **Example:** `69 38 6F 33 01 50 B5 94 34 00 00`  
- **Backend:** Record check-in.

---

### 0x02 — NFC check-out

| Field | Bytes | Format |
|-------|-------|--------|
| ts | 0–3 | uint32 BE |
| evt | 4 | 0x02 |
| uid | 5–8 | 4-byte card UID |
| reserved | 9–10 | 0 |

- **FPort:** 11  
- **When:** Staff NFC check-out. Device updates “Last Cleaned” locally.  
- **Backend:** Record check-out.

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
- **When:** Staff taps NFC then selects button (registered vote).  
- **Backend:** Persist vote with card UID.

---

### 0x10 — Battery status (heartbeat)

| Field | Bytes | Format |
|-------|-------|--------|
| ts | 0–3 | uint32 BE |
| evt | 4 | 0x10 |
| battery_mv | 5–6 | uint16 BE (mV) |
| percent | 7 | 0..100 (0 if unknown) |
| flags | 8 | bitfield (0 for now) |
| reserved | 9–10 | 0 |

- **FPort:** 20  
- **When:** Periodic housekeeping (and on-demand after downlink 0x04).  
- **Backend:** Store battery/health; use as heartbeat. Good window to send downlinks (Class A).
- **Note:** v1.2.1 doesn't implement battery percentage, will always be 0.

**Heartbeat timing (jitter):** Heartbeat runs **once per day** at a device-specific time to spread load. First run of the day (UTC):

`heartbeat_time_UTC = 00:00 UTC + offset_minutes`

`offset_minutes = (DevEUI[7]×256 + DevEUI[6]) % 1440`

DevEUI is 8 bytes (index 0..7); bytes 6 and 7 are the last two. Result is 0..1439 minutes after midnight UTC. Backend can derive each device’s daily heartbeat time from DevEUI for scheduling or “last seen” alerts.

---

\newpage

### 0x12 — Counter sync

| Field | Bytes | Format |
|-------|-------|--------|
| ts | 0–3 | uint32 BE |
| evt | 4 | 0x12 |
| button_id | 5 | 0..5 |
| counter | 6–8 | 24-bit BE |
| reserved | 9–10 | 0 |

- **FPort:** 20  
- **When:** After rejoin and during housekeeping; one uplink per button (rate-limited).  
- **Backend:** Reconcile per-device per-button counters (e.g. after outage).

---

### 0x11 — Low battery Alarm (reserved)

- Defined in firmware; not yet sent. Same FPort as housekeeping when implemented.

---

### 0xFF — Future

- Reserved for new event types.

---

## Downlink: Commands

First byte = command. Device ignores FPort for routing.

| Cmd | Code | Payload | Effect |
|-----|------|---------|--------|
| EPD update | 0x01 | 5 bytes: `01 E3 E2 E1 E0` (epoch uint32 BE) | Set “Last Cleaned” on EPD. |
| EPD refresh | 0x02 | 1 byte: `02` | Full e-paper refresh (clear ghosting). |
| Timezone offset | 0x03 | 3 bytes: `03 HH LL` (int16 BE, minutes from UTC) | EEPROM timezone for EPD “Last Cleaned” display. |
| Status request | 0x04 | 1 byte: `04` | Triggers heartbeat (battery + counter-sync); use RX after next uplink for downlink. |
| Reset counters | 0x05 | 1 byte: `05` | Set all button counters to 0. |
| Factory reset | 0x06 | 1 byte: `06` | Clear counters, devnonce, has_joined_once; device reboots (first-boot behavior). |

---

\newpage

**Timezone examples (0x03 payload = `03` + int16 BE minutes from UTC):**

| Timezone | Offset (min) | Hex payload |
|----------|--------------|-------------|
| **New York** (EST, winter) | −300 | `03 FE D4` |
| **New York** (EDT, summer) | −240 | `03 FF 10` |
| **Denver / Mountain** (MST) | −420 | `03 FE 5C` |
| **Denver / Mountain** (MDT) | −360 | `03 FE 98` |
| **San Francisco / Pacific** (PST) | −480 | `03 FE 20` |
| **San Francisco / Pacific** (PDT) | −420 | `03 FE 5C` |

To switch a device to another timezone: send one downlink with the 3-byte payload above. Device stores it and uses it when rendering “Last Cleaned” on the EPD (UTC epoch + offset = local time). No reboot required.

**How to send:** Queue downlink with payload = command byte (+ optional bytes as above). Any FPort (e.g. 10). Unconfirmed typical; device is Class A so downlink only in RX1/RX2 after an uplink.

**EPD update example:** Epoch 1771131600 → `01 69 91 51 90`. `https://www.epochconverter.com/hex`

---

## Notes

- Downlink only in RX1/RX2 after device uplink. For on-demand status, send **0x04**; device will send battery + counter-sync; and then backend can reply in that RX window (e.g. 0x01 “Last Cleaned”).
- Button IDs: 0..5 (6 buttons). Counter: 24-bit, per-button, big-endian in payload.

---

\newpage

## Example payloads (real uplinks)

Decoded samples from device uplinks. All timestamps are UTC epoch (bytes 0–3, big-endian); byte 4 = event code.

<!-- | FPort | Base64           | Hex                          | Explanation                                                                                  |
|-------|------------------|------------------------------|----------------------------------------------------------------------------------------------|
| 10    | aZ3guwAAAAAEAAA= | 69 9d e0 bb 00 00 00 00 04 00 00 | **Button (0x00):** ts = 0x699DE0BB (~2026-02-24 11:39 UTC), button_id = 0, counter = 4. Public vote on button 0. |
| 11    | aZ3g7wGqqru7AAA= | 69 9d e0 ef 01 aa aa bb bb 00 00 | **NFC check-in (0x01):** ts = 0x699DE0EF, uid = AA AA BB BB. Staff check-in.                 |
| 11    | aZ3hCAKqqru7AAA= | 69 9d e1 08 02 aa aa bb bb 00 00 | **NFC check-out (0x02):** ts = 0x699DE108, uid = AA AA BB BB. Staff check-out.               |
| 11    | aZ3hKQMCqqq7uwA= | 69 9d e1 29 03 02 aa aa bb bb 00 | **NFC vote (0x03):** ts = 0x699DE129, button_id = 2, card = AA AA BB BB. Registered NFC vote for button 2. |
| 20    | aZ3YuRIAAAABAAA= | 69 9d d8 b9 12 00 00 00 01 00 00 | **Counter sync (0x12):** ts = 0x699DD8B9, button_id = 0, counter = 1 (24-bit BE). Rejoin/heartbeat sync for button 0. |
| 20    | aZ3YuRIBAAABAAA= | 69 9d d8 b9 12 01 00 00 01 00 00 | **Counter sync (0x12):** ts = 0x699DD8B9, button_id = 1, counter = 1.                       |
| 20    | aZ3YuRICAAABAAA= | 69 9d d8 b9 12 02 00 00 01 00 00 | **Counter sync (0x12):** ts = 0x699DD8B9, button_id = 2, counter = 1.                       |
| 20    | aZ3YuRIDAAAAAAA= | 69 9d d8 b9 12 03 00 00 00 00 00 | **Counter sync (0x12):** ts = 0x699DD8B9, button_id = 3, counter = 0.                       |
| 20    | aZ3YuRIEAAAAAAA= | 69 9d d8 b9 12 04 00 00 00 00 00 | **Counter sync (0x12):** ts = 0x699DD8B9, button_id = 4, counter = 0.                       |
| 20    | aZ3YuRIFAAAFAAA= | 69 9d d8 b9 12 05 00 00 05 00 00 | **Counter sync (0x12):** ts = 0x699DD8B9, button_id = 5, counter = 5.                       |
| 20    | aZ3ZwBAM1wAAAAA= | 69 9d d9 c0 10 0c d7 00 00 00 00 | **Battery (0x10):** ts = 0x699DD9C0, battery_mv = 0x0CD7 (3287 mV), percent = 0, flags = 0. Heartbeat. | -->

| FPort | Base64 | Hex | Explanation |
|:---|:---|:---|:---|
| 10 | `aZ3guwAAAAAEAAA=` | `69 9d e0 bb 00 00 00 00 04 00 00` | **Button (0x00):** ts = 0x699DE0BB (~2026-02-24 11:39 UTC), button_id = 0, counter = 4. Public vote on button 0. |
| 11 | `aZ3g7wGqqru7AAA=` | `69 9d e0 ef 01 aa aa bb bb 00 00` | **NFC check-in (0x01):** ts = 0x699DE0EF, uid = AA AA BB BB. Staff check-in. |
| 11 | `aZ3hCAKqqru7AAA=` | `69 9d e1 08 02 aa aa bb bb 00 00` | **NFC check-out (0x02):** ts = 0x699DE108, uid = AA AA BB BB. Staff check-out. |
| 11 | `aZ3hKQMCqqq7uwA=` | `69 9d e1 29 03 02 aa aa bb bb 00` | **NFC vote (0x03):** ts = 0x699DE129, button_id = 2, card = AA AA BB BB. Registered NFC vote for button 2. |
| 20 | `aZ3YuRIAAAABAAA=` | `69 9d d8 b9 12 00 00 00 01 00 00` | **Counter sync (0x12):** ts = 0x699DD8B9, button_id = 0, counter = 1 (24-bit BE). Rejoin/heartbeat sync for button 0. |
| 20 | `aZ3YuRIBAAABAAA=` | `69 9d d8 b9 12 01 00 00 01 00 00` | **Counter sync (0x12):** ts = 0x699DD8B9, button_id = 1, counter = 1. |
| 20 | `aZ3YuRICAAABAAA=` | `69 9d d8 b9 12 02 00 00 01 00 00` | **Counter sync (0x12):** ts = 0x699DD8B9, button_id = 2, counter = 1. |
| 20 | `aZ3YuRIDAAAAAAA=` | `69 9d d8 b9 12 03 00 00 00 00 00` | **Counter sync (0x12):** ts = 0x699DD8B9, button_id = 3, counter = 0. |
| 20 | `aZ3YuRIEAAAAAAA=` | `69 9d d8 b9 12 04 00 00 00 00 00` | **Counter sync (0x12):** ts = 0x699DD8B9, button_id = 4, counter = 0. |
| 20 | `aZ3YuRIFAAAFAAA=` | `69 9d d8 b9 12 05 00 00 05 00 00` | **Counter sync (0x12):** ts = 0x699DD8B9, button_id = 5, counter = 5. |
| 20 | `aZ3ZwBAM1wAAAAA=` | `69 9d d9 c0 10 0c d7 00 00 00 00` | **Battery (0x10):** ts = 0x699DD9C0, battery_mv = 0x0CD7 (3287 mV), percent = 0, flags = 0. Heartbeat. |

**Decoding reference:** [0–3] = timestamp (uint32 BE); [4] = event (0x00=button, 0x01/0x02/0x03=NFC in/out/vote, 0x10=battery, 0x12=counter sync); [5..] = event-specific (button_id, counter 24-bit BE, uid, battery_mv uint16 BE, etc.).
