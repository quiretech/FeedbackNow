---
deleted: true
tags: [Import-30db]
title: FB-NOW v1.1 - Functional Requirements Document
created: '2026-01-27T20:14:56.873Z'
modified: '2026-01-27T21:18:56.097Z'
---

# FB-NOW v1.1 - Functional Requirements Document

**Document Version:** 1.0  
**Date:** January 2026  
**Status:** Draft

---

## 1. Overview

FB-NOW v1.1 is a battery-operated LoRaWAN feedback device designed for indoor deployment (e.g., bathrooms, retail spaces, airports). The device captures user sentiment via 6 physical buttons and supports employee check-in/check-out workflows via NFC. An optional e-paper display shows timestamps and status. All data is uplinked to a LoRaWAN network for backend processing.

**Hardware Components:**

| Component | Description |
|-----------|-------------|
| MCU | NRF52840 (Zephyr RTOS) |
| Buttons | 6 physical buttons (0-5) |
| NFC | PN5180 module (ISO15693) |
| LoRa | SX1262 module (Class A, OTAA) |
| Display | E-Paper (optional, Variant A only) |
| Timekeeping | External RTC |
| Storage | EEPROM + SDHC card slot |
| Feedback | 1 LED |
| Power | Replaceable battery (9600mAh), ADC monitoring |
| Unused | Reed switch (reserved for future) |

---

## 2. User Roles & Interactions

### 2.1 User Types

| User Type | Description | Authentication |
|-----------|-------------|----------------|
| **Public** | General foot traffic (customers, visitors) | None |
| **Staff** | Custodians, managers, employees | NFC card (ISO15693, 4-byte NDEF ID) |

### 2.2 Button Layout

| Button | Color | Public Mode | Staff Mode |
|--------|-------|-------------|------------|
| 0 | Green | Positive feedback (Happy) | Check-in |
| 1 | Yellow | Neutral feedback (Satisfied) | Check-out |
| 2 | Red | Issue (per decal) | Registered Vote |
| 3 | Red | Issue (per decal) | Registered Vote |
| 4 | Red | Issue (per decal) | Registered Vote |
| 5 | Red | Issue (per decal) | Registered Vote |

*Note: Red button labels (2-5) vary by deployment location via physical decals.*

### 2.3 Interaction Summary

| Action | User | Trigger | Result |
|--------|------|---------|--------|
| Feedback Vote | Public | Single button press | LED confirmation, LoRa uplink |
| Check-in | Staff | Staff Mode → Button 0 → NFC tap | EPD: "Cleaning in progress", Uplink + NFC uplink per schema |
| Check-out | Staff | Staff Mode → Button 1 → NFC tap | EPD: "Last Cleaned: [time]", Uplink + NFC uplink per schema  |
| Registered Vote | Staff | Staff Mode → Button 2-5 → NFC tap | Uplink with button ID + NFC uplink per schema |

---

## 3. System States & Transitions

### 3.1 Button Combinations

| Combo | Hold Duration | Action |
|-------|---------------|--------|
| Single button (0-5) | Tap | Public feedback vote |
| Button 0+1 | 2 seconds | Enter Staff Mode |
| Button 0+1+5 | 3 seconds | Device Info display |
| All 6 buttons | 10 seconds | Reboot device |

### 3.2 State Machine

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              NORMAL MODE                                     │
│  • Button 0-5 tap → Public vote → LED 1s → Uplink → 5s lockout             │
│  • LED: Off (idle)                                                          │
└─────────────────────────────────────────────────────────────────────────────┘
         │                              │                              │
         │ Btn 0+1 hold 2s              │ Btn 0+1+5 hold 3s            │ All 6 hold 10s
         ▼                              ▼                              ▼
┌─────────────────┐            ┌─────────────────┐            ┌─────────────────┐
│   STAFF MODE    │            │  DEVICE INFO    │            │     REBOOT      │
│ Timeout: 10s    │            │ Timeout: 30s    │            │ LED: Solid 3s   │
│ LED: Solid on   │            │ EPD: Show info  │            │ Then reboot     │
└────────┬────────┘            └────────┬────────┘            └─────────────────┘
         │                              │
         │ Button 0-5 pressed           │ Any button or timeout
         ▼                              ▼
┌─────────────────┐              Back to NORMAL
│    NFC SCAN     │
│ Timeout: 5s     │
│ LED: Blink 1Hz  │
└────────┬────────┘
         │
    ┌────┴────┐
    │         │
    ▼         ▼
 Timeout   Card Read
 or Fail   Success
    │         │
    ▼         ▼
 NORMAL    PROCESS ACTION
              │
              ├─► Btn 0: Check-in → EPD update → Uplink 0x01
              ├─► Btn 1: Check-out → EPD update → Uplink 0x02
              └─► Btn 2-5: Registered Vote → Uplink 0x03
              │
              ▼
           NORMAL
```

### 3.3 State Definitions

| State | Entry Trigger | Timeout | On Timeout | LED |
|-------|---------------|---------|------------|-----|
| Normal | Boot complete / other states exit | None | - | Off |
| Staff Mode | Button 0+1 hold 2s | 10s | Return to Normal | Solid on |
| NFC Scan | Button press in Staff Mode | 5s | Return to Normal | Blink 1Hz |
| Device Info | Button 0+1+5 hold 3s | 30s | Return to Normal | - |
| Reboot | All 6 buttons hold 10s | - | - | Solid 3s |

### 3.4 Timeout Summary

| Context | Timeout | Behavior |
|---------|---------|----------|
| Staff Mode waiting | 10 seconds | Return to Normal (no uplink) |
| NFC Scan waiting | 5 seconds | Return to Normal (no uplink) |
| Device Info display | 30 seconds | Return to Normal |
| Public button lockout | 5 seconds | Accept next press |
| EPD "Thank you" message | 5 seconds | Return to "Last Cleaned" |

---

## 4. Functional Requirements

### 4.1 Button Behavior

**Public Mode (Normal):**
- Single button tap triggers feedback vote
- LED solid for 1 second confirms acceptance
- 5-second lockout prevents spam (silent rejection, no LED)
- Uplink Event 0x00 with button ID and running counter

**Staff Mode:**
- Button press in Staff Mode selects action type and starts NFC scan
- No lockout between actions (staff may need multiple votes)

**Button Counters:**
- Each button maintains independent running counter (24-bit, max 16,777,216)
- Counters persist across power cycles (stored in EEPROM)
- Counters persist across battery replacement

### 4.2 NFC / Staff Mode

**Staff Mode Entry:**
- Button 0+1 held for 2 seconds
- LED turns solid to indicate active state
- 10-second timeout to select action

**NFC Scan Flow:**
1. Staff presses button (0-5) in Staff Mode
2. LED blinks 1Hz (waiting for card)
3. Staff taps ISO15693 card within 5 seconds
4. Device reads 4-byte NDEF user record
5. Action executes based on button pressed

**Actions:**

| Button | Action | Event Type | EPD Update |
|--------|--------|------------|------------|
| 0 | Check-in | 0x01 | "Cleaning in progress" |
| 1 | Check-out | 0x02 | "Last Cleaned: [RTC time]" |
| 2-5 | Registered Vote | 0x03 | No change |

**NFC Failure:**
- Read timeout or error: LED 3 fast blinks, return to Normal

### 4.3 E-Paper Display (Variant A Only)

**Display States:**

| State | Content | Trigger |
|-------|---------|---------|
| Default | "LAST CLEANED AT: [timestamp]" | Boot, Check-out |
| Feedback Ack | "Thank you for your feedback" | Public button press |
| Cleaning | "Cleaning in progress" | Check-in |
| Device Info | DevEUI, FW version, battery %, counters | Button 0+1+5 hold |
| Boot | "Connecting..." | During LoRa join |



**Timing:**
- "Thank you" displays for 5 seconds, then returns to default
- "Cleaning in progress" persists until check-out

**Downlink EPD Updates:**
- Backend can push "Last Cleaned" timestamp via downlink
- EPD downlink updates only processed after heartbeat (not after public button press to avoid UX confusion)

### 4.4 LED Feedback

| State | LED Pattern | Duration |
|-------|-------------|----------|
| Power On | 2 quick flashes | ~200ms |
| Join Success | 3 quick flashes | ~300ms |
| Normal / Idle | Off | - |
| Button Accepted (public) | Solid on | 1 second |
| Button Rejected (lockout) | Off | - |
| Staff Mode Active | Solid on | Until exit |
| NFC Waiting | Blink 1Hz | Until scan/timeout |
| NFC Read Fail | 3 fast blinks | ~600ms |
| Check-in Confirmed | Solid on | 2 seconds |
| Check-out Confirmed | 2 blinks | ~2 seconds |
| Registered Vote Confirmed | 3 quick flashes | ~450ms |
| Reboot Triggered | Solid on → off | 3 seconds → reboot |

### 4.5 LoRa Communication

**Network Configuration:**
- LoRaWAN Class A
- OTAA activation
- US915 frequency plan 
- EU868 frequency plan

**Uplink Event Types:**

| Event Type | Code | Trigger | Payload Contains |
|------------|------|---------|------------------|
| Button Press | 0x00 | Public vote | Timestamp, Button ID, Counter |
| Check-in | 0x01 | Staff NFC | Timestamp, NDEF ID |
| Check-out | 0x02 | Staff NFC | Timestamp, NDEF ID |
| Registered Vote | 0x03 | Staff NFC | Timestamp, NDEF ID, Button ID |
| Heartbeat | 0x04 | Daily timer | Timestamp, Battery %, Status |
| Low Battery | 0x05 | Threshold crossed | Timestamp, Battery % |

**Downlink Commands:**

| Command | Code | Description |
|---------|------|-------------|
| Update EPD | 0x01 | Set "Last Cleaned" timestamp |
| EPD Refresh | 0x02 | Full e-paper refresh (clear ghosting) |
| Status Request | 0x03 | Trigger status uplink |
| Reset Counters | 0x04 | Reset button counters to zero |

**Heartbeat:**
- Frequency: Once daily
- Timing: DevEUI-based jitter (distributes load across 24 hours)
- Purpose: Battery status, RTC sync opportunity, downlink delivery window

### 4.6 Storage

**EEPROM:**
- Button counters (6 × 24-bit)
- LoRa DevNonce (replay protection)
- Configuration data
- CRC validation on read

**SDHC Card:**
- Daily log files (button counts, events)
- Counter backup
- Optional - device operates without card (logging disabled)

**Data Persistence:**
- Counters written to EEPROM on each button press
- Daily flush from EEPROM to SDHC during housekeeping

### 4.7 Power Management

**Battery:**
- Replaceable, non-rechargeable
- Capacity: 9600mAh (1-2 cells)

**Low Battery:**
- ADC monitors battery voltage
- Below threshold: Uplink Event 0x05
- No local LED indication (rely on backend alerts)

**Sleep Modes:**
- Deep sleep between events (button GPIO + RTC alarm wake)
- Light sleep during NFC scan timeout

### 4.8 Boot Sequence

```
1. POWER ON
   └─► LED: 2 flashes

2. HARDWARE INIT
   └─► GPIO, SPI, I2C, ADC

3. EEPROM INIT
   └─► Load config, counters, DevNonce
   └─► Validate CRC (use defaults if fail)

4. SDHC INIT
   └─► Mount card (continue without if fail)

5. RTC INIT
   └─► Read time, flag if invalid

6. EPD INIT (Variant A)
   └─► Display "Connecting..."

7. LORA INIT
   └─► Initialize SX1262, load credentials

8. LORA JOIN
   └─► OTAA join with retry (10 attempts, exponential backoff)
   └─► Fail after 3 boot cycles → shutdown

9. RTC SYNC
   └─► DeviceTimeReq MAC command
   └─► Update RTC from network time

10. EPD UPDATE (Variant A)
    └─► Display "Last Cleaned: [RTC time]"

11. BOOT UPLINK
    └─► Event 0x04 (status), opens RX window

12. NORMAL MODE
    └─► LED: 3 flashes (join success)
    └─► Enable button interrupts
    └─► Start heartbeat timer
    └─► Enter low-power wait
```

### 4.9 Housekeeping

Housekeeping tasks run during daily heartbeat:

| Task | Description |
|------|-------------|
| RTC Resync | DeviceTimeReq MAC command (correct drift) |
| Battery Sample | ADC reading for heartbeat payload |
| EEPROM → SDHC Flush | Persist counters to SD card |
| Counter Validation | CRC check, repair from backup if needed |
| SDHC Health | Check remaining space, flag if low |
| EPD Refresh | Full refresh every 7 days (clear ghosting) |

---

## 5. Firmware Variants

Two firmware variants are built from the same codebase with compile-time flags.

### 5.1 Variant Comparison

| Feature | Variant A (with EPD) | Variant B (without EPD) |
|---------|---------------------|------------------------|
| E-Paper Display | Yes | No |
| Button Feedback | LED + EPD text | LED only |
| "Last Cleaned" display | Shown on EPD | Not applicable |
| "Cleaning in progress" | Shown on EPD | LED pattern only |
| "Thank you" message | Shown on EPD | LED only |
| Device Info display | Shown on EPD | Status uplink triggered |
| NFC / Staff Mode | Full support | Full support |
| Check-in/out | Full support | Full support (no timestamp display) |
| Downlink EPD update | Supported | Command ignored |
| All other features | Identical | Identical |

### 5.2 Use Cases

- **Variant A:** Deployments requiring visible "Last Cleaned" timestamp (bathrooms)
- **Variant B:** Simpler feedback-only deployments

### 5.3 Build Configuration

Single codebase with `CONFIG_EPD_ENABLED` flag:
- `CONFIG_EPD_ENABLED=y` → Variant A
- `CONFIG_EPD_ENABLED=n` → Variant B

---

## 6. Error Handling

### 6.1 Error Conditions & Recovery

| Error | Detection | Behavior | Recovery |
|-------|-----------|----------|----------|
| **LoRa Join Failure** | No JoinAccept after attempt | Retry with exponential backoff | 10 retries per boot, 3 boot cycles then shutdown |
| **NFC Read Failure** | Timeout or read error | LED: 3 fast blinks | Return to Normal Mode |
| **NFC Read Timeout** | 5s with no card tap | LED off | Return to Normal Mode |
| **SDHC Mount Failure** | Mount returns error | Set flag, continue operation | Device operates without logging |
| **SDHC Full** | Space check during housekeeping | Uplink warning (future) | Continue operation, oldest logs may be lost |
| **EEPROM CRC Failure** | CRC mismatch on read | Use defaults, set first-boot flag | Attempt repair from SDHC backup |
| **RTC Invalid** | Year < 2024 | Flag for sync | Sync via DeviceTimeReq after join |
| **Low Battery** | ADC below threshold | Uplink Event 0x05 | Continue until power loss |

### 6.2 Join Failure Escalation

```
Boot 1: Attempt join (10 tries with backoff)
        └─► Fail → Reboot

Boot 2: Attempt join (10 tries with backoff)
        └─► Fail → Reboot

Boot 3: Attempt join (10 tries with backoff)
        └─► Fail → SHUTDOWN (requires battery removal to retry)
```

---

## 7. Customer Decision Items

The following items require customer input before implementation:

| # | Item | Options |
|---|------|---------|
| C1 | **"Cleaning in progress" button blocking** | A) Block public buttons during cleaning<br>B) Allow votes during cleaning |
| C2 | **Custodian no-checkout timeout** | A) EPD stays "Cleaning in progress" indefinitely<br>B) Auto-revert after X hours<br>C) Auto-revert to previous timestamp<br>D) Auto-revert to current time |
| C3 | **Downlink EPD latency** | Once-daily heartbeat = up to 24hr delay for EPD updates |


---

## 8. Assumptions

### 8.1 Confirmed Design Decisions

| # | Item | Decision |
|---|------|----------|
| A1 | Button IDs | 0-5 (6 total) |
| A2 | Staff Mode entry | Button 0+1 hold 2 seconds |
| A3 | Device Info entry | Button 0+1+5 hold 3 seconds |
| A4 | Reboot trigger | All 6 buttons hold 10 seconds |
| A5 | NFC data format | 4-byte NDEF user record (not 8-byte UID) |
| A6 | NFC card type | ISO15693 |
| A7 | Device is stateless | Backend handles session logic (check-in/out pairing) |
| A8 | Public lockout | 5 seconds between presses |
| A9 | Staff lockout | None (multiple actions allowed) |
| A10 | Heartbeat frequency | Once daily |
| A11 | Heartbeat timing | DevEUI-based jitter (distributed across 24 hours) |
| A12 | RTC sync | DeviceTimeReq MAC command after join |
| A13 | Initial "Last Cleaned" | RTC time at first boot |
| A14 | Counter persistence | Survives power cycle and battery replacement |
| A15 | Counter size | 24-bit per button (max 16,777,216) |
| A16 | Reed switch | Hardware present, unused in v1.1 |
| A17 | Single LED | All UI feedback via one LED |
| A18 | LoRaWAN Class | Class A |
| A19 | Activation | OTAA |
| A20 | Join retry | 10 attempts per boot, 3 boot cycles then shutdown |


---

## Document History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | January 2026 | Initial draft |

---
