# LoRaWAN device firmware requirements
_Exported on 1/26/2026 at 16:43:33 EST from Cursor (2.3.35)_

---

**User**

@cursor_lorawan_device_firmware_requirem.md 

This file contains my converstaion from past chat. Use it to brief yourself on what I am planning. We will continue our brainstorming where we left off

---

**Cursor**

I've reviewed your previous conversation. Here's a summary of where you left off:

---

## Project Summary: FB-NOW v1.1 - LoRaWAN Feedback Device

**Hardware:** NRF52840 (Zephyr), 6 buttons, PN5180 NFC, SX1262 LoRa, E-Paper, RTC, SDHC, EEPROM, 1 LED, battery ADC

**Core Use Case:** Bathroom feedback device - public presses buttons to give feedback, employees use NFC for check-in/out and "registered votes"

---

## Key Design Decisions Already Locked

| Item | Decision |
|------|----------|
| Button IDs | 0-5 (6 total), reed switch unused |
| NFC Mode Entry | Button 0+5 hold 2s |
| NFC Action Selection | Button 0 = Check-in, Button 5 = Check-out, Buttons 1-4 = Registered Vote |
| NFC Data | 4-byte NDEF user ID (not 8-byte UID) |
| Device Logic | Stateless - backend handles all interpretation |
| Lockout | 5 seconds, public users only |
| Heartbeat | Daily with DevEUI-based jitter |
| RTC Sync | DeviceTimeReq MAC command after join |
| EPD States | "Last Cleaned", "Thank you for feedback", "Cleaning in progress" |
| Firmware Variants | Two: with EPD and without EPD |

---

## Open Questions Awaiting Your Input

The conversation ended with these unanswered questions:

1. **No-EPD variant use case** - Why would a customer choose the no-EPD version? Helps determine LED feedback needs for that variant.

2. **Credential provisioning** - How do DevEUI/AppEUI/AppKey get programmed into each device? (Factory SWD? Serial provisioning? Derived from chip ID?)

3. **Factory reset** - Should technicians be able to reset a device in the field? What should reset (counters, LoRa session, SD logs)?

4. **Watchdog** - Do you want hardware watchdog protection? (Recommended for unattended devices)

5. **Button sequences** - Confirm these work:
   - Device info: Button 0+1+5 hold 3s
   - NFC mode: Button 0+5 hold 2s
   - Factory reset: All 6 buttons hold 10s?

6. **LED behavior** - The proposed patterns (slow blink for joining, fast blink for NFC waiting, solid for accepted press, off for rejected) - any adjustments?

---

## Questions Still Needing Customer Validation

- Does "Cleaning in progress" block customer button presses?
- If custodian forgets to check out, should EPD auto-revert after X hours? To what time?

---

Where would you like to pick up? I can help you work through the open technical questions, refine the boot/housekeeping flows, nail down LED UI patterns, or discuss any other aspect of the system.

---

**User**

No-EPD variant use case - Why would a customer choose the no-EPD version? Helps determine LED feedback needs for that variant.
-- just a perference, some usecases just need button uplink and no epd for extra info. i just had the idea of epd for on field debugging or easy way to see whats flashed before packing in the box. cz since we have epd, might as well use it.

Credential provisioning - How do DevEUI/AppEUI/AppKey get programmed into each device? (Factory SWD? Serial provisioning? Derived from chip ID?)
-- hmm. im thinking this as of now, ill have a python script that will generate random euis, and then it will open up my eui_keys.h, update it and then ill west flash. is there any other better way you suggest? 


Factory reset - Should technicians be able to reset a device in the field? What should reset (counters, LoRa session, SD logs)?
-- yes i think we should allow on field to reboot the device. not erase coutners, but just reboot. so that they dont need to open the device to take battery out. they can just press buttons in some order to reboot the device.


Watchdog - Do you want hardware watchdog protection? (Recommended for unattended devices)
-- not sure at the moment.

Button sequences - Confirm these work:
Device info: Button 0+1+5 hold 3s
NFC mode: Button 0+5 hold 2s
Factory reset: All 6 buttons hold 10s?
-- yep these work.

LED behavior - The proposed patterns (slow blink for joining, fast blink for NFC waiting, solid for accepted press, off for rejected) - any adjustments?

not sure about led. lets lock all conditions where we wuld need led ui and then we lock it down. since led is our only point of UI response

---

**Cursor**

Good progress! Let me address the credential provisioning question and then compile a comprehensive LED UI state list.

---

## Credential Provisioning - Better Approaches

Your current approach (Python → update header → flash) works but has drawbacks:
- Manual per-device process
- Credentials compiled into firmware binary (harder to audit, can't change without reflash)
- No separation between firmware and device identity

**Recommended Approach: EEPROM-Based Provisioning**

```
┌─────────────────────────────────────────────────────────────────┐
│  MANUFACTURING FLOW                                              │
├─────────────────────────────────────────────────────────────────┤
│  1. Flash SAME firmware binary to all devices                    │
│  2. Run provisioning tool (Python + serial/RTT) that:           │
│     • Generates DevEUI/AppEUI/AppKey                            │
│     • Writes to EEPROM via simple serial protocol               │
│     • Logs credentials to CSV for LNS registration              │
│  3. Device reads credentials from EEPROM at boot                │
└─────────────────────────────────────────────────────────────────┘
```

**Benefits:**
- Single firmware binary for all units (simpler CI/CD, easier testing)
- Credentials auditable in EEPROM, not hidden in binary
- Can re-provision device without reflashing firmware
- Provisioning tool can auto-register to LNS via API

**Alternative: Derive DevEUI from Chip ID**

nRF52840 has a unique 64-bit device ID in FICR (Factory Information Configuration Registers). You could:
- Use FICR as DevEUI directly (guaranteed unique per chip)
- Still need to provision AppEUI and AppKey externally

**My Recommendation:** Hybrid approach
- DevEUI = nRF52840 FICR unique ID (no provisioning needed)
- AppEUI = Same for all devices in a deployment (compiled in firmware)
- AppKey = Generated per device, stored in EEPROM via provisioning tool

This minimizes provisioning to just the AppKey.

---

## Comprehensive LED UI State List

Let me enumerate **every state where LED feedback is needed**:

### Boot / Initialization States

| State | Trigger | Duration | Notes |
|-------|---------|----------|-------|
| Power On | Battery inserted / reboot | Momentary | Confirms device alive |
| Hardware Init | During peripheral init | 1-2 seconds | Optional - may skip for power |
| LoRa Joining | During OTAA join attempts | Until success/fail | Ongoing feedback |
| Join Success | Network joined | Momentary | Confirm good state |
| Join Failed (retrying) | Single attempt failed | Momentary | Indicates retry |
| Join Failed (giving up) | All retries exhausted | Before reboot/shutdown | Critical error |
| Shutdown (unrecoverable) | 3 boot cycles failed | Before power down | Critical error |

### Normal Operation States

| State | Trigger | Duration | Notes |
|-------|---------|----------|-------|
| Idle / Ready | Normal operation | N/A | LED off (save power) |
| Button Press Accepted | Valid button press | ~1 second | Confirms vote registered |
| Button Press Rejected | During 5s lockout | N/A | No LED (silent rejection) |
| LoRa TX in Progress | Uplink transmitting | During TX | Optional - may skip |
| LoRa TX Success | ACK received (if confirmed) | Momentary | Optional |
| LoRa TX Failed | No ACK / channel busy | Momentary | Optional |

### NFC Mode States

| State | Trigger | Duration | Notes |
|-------|---------|----------|-------|
| Entering NFC Mode | Button 0+5 held 2s | At entry | Confirm mode change |
| NFC Waiting for Card | In NFC mode | Until tap/timeout | Ongoing "ready" indicator |
| NFC Read Success | Card read OK | ~1-2 seconds | Confirm read |
| NFC Read Failed | Card read error | ~1 second | Indicate failure |
| Awaiting Button (post-scan) | After successful NFC read | Until button/timeout | Waiting for IN/OUT/Vote |
| NFC Mode Timeout | No card in 5s | At exit | Indicate mode exit |

### Employee Action Confirmation

| State | Trigger | Duration | Notes |
|-------|---------|----------|-------|
| Check-in Confirmed | Button 0 after NFC scan | ~1-2 seconds | Confirm check-in |
| Check-out Confirmed | Button 5 after NFC scan | ~1-2 seconds | Confirm check-out |
| Registered Vote Confirmed | Button 1-4 after NFC scan | ~1-2 seconds | Confirm vote |

### Special Modes

| State | Trigger | Duration | Notes |
|-------|---------|----------|-------|
| Device Info Mode | Button 0+1+5 held 3s | While in mode | Indicate special mode |
| Reboot Triggered | All 6 buttons held 10s | Before reboot | Confirm reboot coming |

### Error / Warning States

| State | Trigger | Duration | Notes |
|-------|---------|----------|-------|
| Low Battery | Below threshold | Periodic? Always? | Alert user |
| SDHC Error | Mount failed / full | At detection | Optional |
| RTC Error | Time invalid | At detection | Optional |
| EEPROM Error | CRC fail | At detection | Optional |

### Housekeeping (Optional)

| State | Trigger | Duration | Notes |
|-------|---------|----------|-------|
| Heartbeat TX | Daily heartbeat | During TX | Optional indicator |

---

## Questions to Lock Down LED UI

1. **Error indication priority** - Should LED indicate errors like SDHC missing, or silently continue? (Device still works, just no logging)

2. **Low battery behavior** - Options:
   - A) Blink once every minute while low
   - B) Blink pattern on every button press when low
   - C) Only indicate on device info query
   - D) No local indication (rely on backend alerts)

3. **TX indication** - Show LED during LoRa TX? (Costs power, but gives user confidence their press "did something")

4. **Variant B (no-EPD) differences** - For check-in/check-out confirmation, EPD variant shows text. No-EPD variant needs:
   - Different LED patterns for check-in vs check-out?
   - Or same pattern (backend handles differentiation anyway)?

5. **NFC read failure** - What should happen?
   - A) Distinct "error" pattern (e.g., 3 fast blinks)
   - B) No LED, just timeout back to normal
   - C) LED off immediately (vs. success = LED on)

6. **Reboot confirmation** - When all 6 buttons held for 10s:
   - Should LED do anything special before reboot?
   - How does user know "keep holding" vs "done, releasing"?

---

Let me know your preferences on these, and we can finalize the LED UI specification. Once LED is locked, we'll have covered all major functional aspects.

---

**User**

I'll tell you which states need UI feedback. after that suggest me what feedback we should actually give. 
IF I send --Y that means i need, if nothing besides it we dont need 

Boot / Initialization States
State	Trigger	Duration	Notes
Power On	Battery inserted / reboot	Momentary	Confirms device alive -- Y
Hardware Init	During peripheral init	1-2 seconds	Optional - may skip for power
LoRa Joining	During OTAA join attempts	Until success/fail	Ongoing feedback  
Join Success	Network joined	Momentary	Confirm good state -- Y
Join Failed (retrying)	Single attempt failed	Momentary	Indicates retry
Join Failed (giving up)	All retries exhausted	Before reboot/shutdown	Critical error
Shutdown (unrecoverable)	3 boot cycles failed	Before power down	Critical error
Normal Operation States
State	Trigger	Duration	Notes
Idle / Ready	Normal operation	N/A	LED off (save power)
Button Press Accepted	Valid button press	~1 second	Confirms vote registered -- Y
Button Press Rejected	During 5s lockout	N/A	No LED (silent rejection)
LoRa TX in Progress	Uplink transmitting	During TX	Optional - may skip
LoRa TX Success	ACK received (if confirmed)	Momentary	Optional
LoRa TX Failed	No ACK / channel busy	Momentary	Optional
NFC Mode States
State	Trigger	Duration	Notes
Entering NFC Mode	Button 0+5 held 2s	At entry	Confirm mode change -- Y
NFC Waiting for Card	In NFC mode	Until tap/timeout	Ongoing "ready" indicator -- Y
NFC Read Success	Card read OK	~1-2 seconds	Confirm read -- Y
NFC Read Failed	Card read error	~1 second	Indicate failure -- Y
Awaiting Button (post-scan)	After successful NFC read	Until button/timeout	Waiting for IN/OUT/Vote -- Y
NFC Mode Timeout	No card in 5s	At exit	Indicate mode exit  -- Y
Employee Action Confirmation
State	Trigger	Duration	Notes
Check-in Confirmed	Button 0 after NFC scan	~1-2 seconds	Confirm check-in -- Y
Check-out Confirmed	Button 5 after NFC scan	~1-2 seconds	Confirm check-out -- Y
Registered Vote Confirmed	Button 1-4 after NFC scan	~1-2 seconds	Confirm vote -- Y
Special Modes
State	Trigger	Duration	Notes
Device Info Mode	Button 0+1+5 held 3s	While in mode	Indicate special mode
Reboot Triggered	All 6 buttons held 10s	Before reboot	Confirm reboot coming
Error / Warning States
State	Trigger	Duration	Notes
Low Battery	Below threshold	Periodic? Always?	Alert user
SDHC Error	Mount failed / full	At detection	Optional
RTC Error	Time invalid	At detection	Optional
EEPROM Error	CRC fail	At detection	Optional
Housekeeping (Optional)
State	Trigger	Duration	Notes
Heartbeat TX	Daily heartbeat	During TX	Optional indicator

Questions to Lock Down LED UI
Error indication priority - Should LED indicate errors like SDHC missing, or silently continue? (Device still works, just no logging)
nah

Low battery behavior - Options:
A) Blink once every minute while low
B) Blink pattern on every button press when low
C) Only indicate on device info query
D) No local indication (rely on backend alerts)
- nah dont need battery status, we just uplink. most of the time device is client facing so keep it minimal

TX indication - Show LED during LoRa TX? (Costs power, but gives user confidence their press "did something")
-- nah

Variant B (no-EPD) differences - For check-in/check-out confirmation, EPD variant shows text. No-EPD variant needs:
-- non epd variant wont have nfc. even if it doesn, we keep the led confimration the same. in that case we just dont show timestamp on epd, however device will still supprot checkin checkout, and registerde vote for manager as its still helpfull on bacedn


Different LED patterns for check-in vs check-out?
Or same pattern (backend handles differentiation anyway)? -- hmm might have to diferent since we need the custodial staff to know atleast whats happeing

NFC read failure - What should happen?
A) Distinct "error" pattern (e.g., 3 fast blinks)
B) No LED, just timeout back to normal
C) LED off immediately (vs. success = LED on)

Reboot confirmation - When all 6 buttons held for 10s:
Should LED do anything special before reboot?
How does user know "keep holding" vs "done, releasing"?
-- yes the led will tell that our rebbot request is good and will do something before it shutsdown for reboot

---

**Cursor**

Got it. Let me compile the final LED UI states and propose specific patterns.

---

## Final LED UI States (Only those marked Y)

### Confirmed States Requiring LED Feedback

| # | State | Category |
|---|-------|----------|
| 1 | Power On | Boot |
| 2 | Join Success | Boot |
| 3 | Button Press Accepted | Normal |
| 4 | Entering NFC Mode | NFC |
| 5 | NFC Waiting for Card | NFC |
| 6 | NFC Read Success | NFC |
| 7 | NFC Read Failed | NFC |
| 8 | Awaiting Button (post-scan) | NFC |
| 9 | NFC Mode Timeout | NFC |
| 10 | Check-in Confirmed | Employee |
| 11 | Check-out Confirmed | Employee |
| 12 | Registered Vote Confirmed | Employee |
| 13 | Reboot Triggered | Special |

---

## Proposed LED Patterns

Using a single LED, here are distinguishable patterns:

### Pattern Legend
- **Solid** = LED continuously on
- **Blink** = On/Off cycle (specified Hz)
- **Flash** = Very brief on (50-100ms)
- **Pulse** = Gradual on/off (if PWM available, otherwise treat as blink)

---

### Boot States

| State | Pattern | Duration | Rationale |
|-------|---------|----------|-----------|
| **Power On** | 2 quick flashes | 200ms total | Quick "alive" confirmation |
| **Join Success** | 3 quick flashes | 300ms total | Distinct from power-on, signals "connected" |

---

### Normal Operation

| State | Pattern | Duration | Rationale |
|-------|---------|----------|-----------|
| **Button Press Accepted** | Solid ON | 1 second | Clear confirmation to customer |

---

### NFC Mode Flow

| State | Pattern | Duration | Rationale |
|-------|---------|----------|-----------|
| **Entering NFC Mode** | 1 long flash | 500ms | Confirms mode entry |
| **NFC Waiting for Card** | Slow blink (1 Hz) | Until tap/timeout | "Waiting" indicator, not urgent |
| **NFC Read Success** | Solid ON | 500ms then transition | Brief confirmation before next state |
| **NFC Read Failed** | 3 fast blinks (5 Hz) | 600ms then exit | Distinct error pattern |
| **Awaiting Button (post-scan)** | Fast blink (2 Hz) | Until button/timeout | "Action needed" - faster = more urgent |
| **NFC Mode Timeout** | LED OFF (immediate) | - | Silent exit, no fanfare |

---

### Employee Action Confirmation

These need to be **distinguishable** so custodial staff knows what happened:

| State | Pattern | Duration | Rationale |
|-------|---------|----------|-----------|
| **Check-in Confirmed** | 1 long solid | 2 seconds | Single = "1" = IN |
| **Check-out Confirmed** | 2 solid blinks | 2 seconds (1s on, 200ms off, 800ms on) | Double = "2" = OUT |
| **Registered Vote Confirmed** | 3 quick flashes | 600ms | Triple = vote recorded |

**Memory aid for staff:** 
- IN = 1 blink (you're starting ONE task)
- OUT = 2 blinks (you're DONE, TWO actions complete: in + out)
- VOTE = 3 quick (distinct from in/out)

---

### Special Mode

| State | Pattern | Duration | Rationale |
|-------|---------|----------|-----------|
| **Reboot Triggered** | Solid ON → then OFF | 3 seconds solid, then off, then reboot | Long hold confirms "request accepted", off = rebooting |

**Reboot flow UX:**
1. User holds all 6 buttons
2. At 10 seconds: LED turns solid ON (confirms "reboot will happen")
3. User can release buttons
4. LED stays on for 3 seconds (grace period / confirmation)
5. LED turns OFF
6. Device reboots

---

## Complete LED State Diagram

```
                                    ┌─────────────────┐
                                    │   POWER ON      │
                                    │ 2 quick flashes │
                                    └────────┬────────┘
                                             │
                                             ▼
                                    ┌─────────────────┐
                                    │  JOINING...     │
                                    │  (no LED)       │
                                    └────────┬────────┘
                                             │
                                             ▼
                                    ┌─────────────────┐
                                    │  JOIN SUCCESS   │
                                    │ 3 quick flashes │
                                    └────────┬────────┘
                                             │
                                             ▼
┌────────────────────────────────────────────────────────────────────────┐
│                           NORMAL MODE (LED OFF)                         │
├────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Button 0-5 pressed ──────► Solid 1s ──────► Back to idle              │
│                                                                         │
│  Button 0+5 held 2s ──────► Enter NFC Mode                             │
│                                                                         │
│  Button 0+1+5 held 3s ────► Device Info (no LED, EPD shows info)       │
│                                                                         │
│  All 6 held 10s ──────────► Solid 3s ──► OFF ──► REBOOT                │
│                                                                         │
└────────────────────────────────────────────────────────────────────────┘
                                             │
                         (Button 0+5 held 2s)│
                                             ▼
                              ┌──────────────────────────┐
                              │      NFC MODE            │
                              │  1 long flash (entry)    │
                              │  Then: Slow blink 1Hz    │
                              │  (waiting for card)      │
                              └──────────────┬───────────┘
                                             │
                          ┌──────────────────┼──────────────────┐
                          │                  │                  │
                          ▼                  ▼                  ▼
                   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
                   │  TIMEOUT    │    │ READ FAIL   │    │ READ OK     │
                   │  (5 sec)    │    │ 3 fast blink│    │ Solid 500ms │
                   │  LED OFF    │    │ then exit   │    │             │
                   └──────┬──────┘    └──────┬──────┘    └──────┬──────┘
                          │                  │                  │
                          ▼                  ▼                  ▼
                   Back to Normal     Back to Normal    ┌───────────────┐
                                                        │ AWAIT BUTTON  │
                                                        │ Fast blink 2Hz│
                                                        │ (10s timeout) │
                                                        └───────┬───────┘
                                                                │
                          ┌─────────────────┬───────────────────┼───────────────────┐
                          │                 │                   │                   │
                          ▼                 ▼                   ▼                   ▼
                   ┌─────────────┐   ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
                   │  TIMEOUT    │   │  BUTTON 0   │     │  BUTTON 5   │     │ BUTTON 1-4  │
                   │  LED OFF    │   │  CHECK-IN   │     │  CHECK-OUT  │     │ REG. VOTE   │
                   │             │   │ Solid 2s    │     │ 2 blinks    │     │ 3 flashes   │
                   └──────┬──────┘   └──────┬──────┘     └──────┬──────┘     └──────┬──────┘
                          │                 │                   │                   │
                          ▼                 ▼                   ▼                   ▼
                   Back to Normal    Back to Normal      Back to Normal      Back to Normal
                                     + EPD: "Cleaning    + EPD: "Last        + No EPD change
                                       in progress"       Cleaned: [time]"
```

---

## Summary Table

| # | State | LED Pattern | Timing |
|---|-------|-------------|--------|
| 1 | Power On | 2 quick flashes | 50ms on, 50ms off, 50ms on |
| 2 | Join Success | 3 quick flashes | 50ms on, 50ms off × 3 |
| 3 | Button Press Accepted | Solid ON | 1000ms |
| 4 | Entering NFC Mode | 1 long flash | 500ms |
| 5 | NFC Waiting for Card | Slow blink | 500ms on, 500ms off (1 Hz) |
| 6 | NFC Read Success | Solid ON | 500ms |
| 7 | NFC Read Failed | 3 fast blinks | 100ms on, 100ms off × 3 |
| 8 | Awaiting Button | Fast blink | 250ms on, 250ms off (2 Hz) |
| 9 | NFC Mode Timeout | OFF | Immediate |
| 10 | Check-in Confirmed | 1 solid | 2000ms |
| 11 | Check-out Confirmed | 2 blinks | 1000ms on, 200ms off, 800ms on |
| 12 | Registered Vote Confirmed | 3 quick flashes | 150ms on, 100ms off × 3 |
| 13 | Reboot Triggered | Solid → OFF → Reboot | 3000ms solid, then off |

---

Does this LED UI specification look good? Any patterns you want to adjust? Once confirmed, we'll have the complete functional picture locked down.

---

**User**

ok this is great but i think im a little unclear how we are doing the nfc in and out vs registed vote. how is our statemachine differentailing what is check in and what was a registed vote. can you clearly lay our how would both scneratos go? if therese a logically issue we can rework it.

---

**Cursor**

Good question - let me walk through both scenarios step-by-step to verify the logic is sound.

---

## Current Proposed Flow

**Single Entry Point:** Button 0+5 hold 2s enters "NFC Mode"

**After card scan, button determines action:**
- Button 0 = Check-in
- Button 5 = Check-out  
- Button 1-4 = Registered Vote

---

## Scenario 1: Custodian Check-in → Clean → Check-out

```
┌─────────────────────────────────────────────────────────────────────────┐
│  CUSTODIAN ARRIVES TO CLEAN                                             │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Step 1: Hold Button 0+5 for 2 seconds                                  │
│          LED: 1 long flash (500ms) - confirms NFC mode entry            │
│                                                                         │
│  Step 2: LED starts slow blink (1Hz) - "waiting for card"               │
│                                                                         │
│  Step 3: Custodian taps their ISO15693 card                             │
│          LED: Solid 500ms - read success                                │
│                                                                         │
│  Step 4: LED starts fast blink (2Hz) - "awaiting button selection"      │
│          (10 second timeout)                                            │
│                                                                         │
│  Step 5: Custodian presses BUTTON 0 (meaning: "I'm checking IN")        │
│          LED: Solid 2 seconds - check-in confirmed                      │
│                                                                         │
│  Step 6: System actions:                                                │
│          • Uplink: Event 0x01 (Check-in) + Timestamp + NDEF ID          │
│          • EPD: Updates to "Cleaning in progress"                       │
│          • Returns to normal mode                                       │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘

        ⏳ Custodian cleans bathroom... (minutes pass)

┌─────────────────────────────────────────────────────────────────────────┐
│  CUSTODIAN FINISHES CLEANING                                            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Step 1: Hold Button 0+5 for 2 seconds                                  │
│          LED: 1 long flash (500ms)                                      │
│                                                                         │
│  Step 2: LED slow blink (1Hz)                                           │
│                                                                         │
│  Step 3: Custodian taps their card again                                │
│          LED: Solid 500ms                                               │
│                                                                         │
│  Step 4: LED fast blink (2Hz)                                           │
│                                                                         │
│  Step 5: Custodian presses BUTTON 5 (meaning: "I'm checking OUT")       │
│          LED: 2 blinks - check-out confirmed                            │
│                                                                         │
│  Step 6: System actions:                                                │
│          • Uplink: Event 0x02 (Check-out) + Timestamp + NDEF ID         │
│          • EPD: Updates to "Last Cleaned: 14:35" (from RTC)             │
│          • Returns to normal mode                                       │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Scenario 2: Manager Registered Vote

```
┌─────────────────────────────────────────────────────────────────────────┐
│  MANAGER REPORTS SMELL ISSUE (Button 4 = smell in this example)         │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Step 1: Hold Button 0+5 for 2 seconds                                  │
│          LED: 1 long flash (500ms)                                      │
│                                                                         │
│  Step 2: LED slow blink (1Hz) - waiting for card                        │
│                                                                         │
│  Step 3: Manager taps their card                                        │
│          LED: Solid 500ms                                               │
│                                                                         │
│  Step 4: LED fast blink (2Hz) - awaiting button                         │
│                                                                         │
│  Step 5: Manager presses BUTTON 4 (the "smell" button per decal)        │
│          LED: 3 quick flashes - registered vote confirmed               │
│                                                                         │
│  Step 6: System actions:                                                │
│          • Uplink: Event 0x03 (Reg Vote) + Timestamp + NDEF ID + Btn 4  │
│          • EPD: NO CHANGE (stays at "Last Cleaned: XX:XX")              │
│          • Returns to normal mode                                       │
│                                                                         │
│  Backend now knows: Manager [NDEF ID] reported issue [Button 4]         │
│  at [Timestamp] for this device location.                               │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## State Machine Diagram

```
                              ┌─────────────────┐
                              │  NORMAL MODE    │
                              │  (LED off)      │
                              └────────┬────────┘
                                       │
                                       │ Button 0+5 held 2s
                                       ▼
                              ┌─────────────────┐
                              │  NFC_SCAN_MODE  │
                              │  LED: Slow 1Hz  │
                              │  Timeout: 5s    │
                              └────────┬────────┘
                                       │
               ┌───────────────────────┼───────────────────────┐
               │                       │                       │
               ▼                       ▼                       ▼
        ┌─────────────┐         ┌─────────────┐         ┌─────────────┐
        │  TIMEOUT    │         │  READ_FAIL  │         │  READ_OK    │
        │  (5s)       │         │             │         │             │
        └──────┬──────┘         └──────┬──────┘         └──────┬──────┘
               │                       │                       │
               ▼                       ▼                       ▼
        Back to NORMAL          Back to NORMAL         ┌─────────────────┐
                                                       │ NFC_AWAIT_BTN   │
                                                       │ LED: Fast 2Hz   │
                                                       │ Timeout: 10s    │
                                                       │ Card ID stored  │
                                                       └────────┬────────┘
                                                                │
        ┌───────────────────┬───────────────────┬───────────────┼───────────────────┐
        │                   │                   │               │                   │
        ▼                   ▼                   ▼               ▼                   ▼
 ┌─────────────┐     ┌─────────────┐     ┌─────────────┐ ┌─────────────┐     ┌─────────────┐
 │  TIMEOUT    │     │  BUTTON 0   │     │  BUTTON 5   │ │ BUTTON 1-4  │     │   (other)   │
 │  (10s)      │     │             │     │             │ │             │     │             │
 └──────┬──────┘     └──────┬──────┘     └──────┬──────┘ └──────┬──────┘     └──────┬──────┘
        │                   │                   │               │                   │
        │                   │                   │               │                   │
        ▼                   ▼                   ▼               ▼                   ▼
 Back to NORMAL      ┌─────────────┐     ┌─────────────┐ ┌─────────────┐     Back to NORMAL
 (no uplink)         │  CHECK_IN   │     │  CHECK_OUT  │ │  REG_VOTE   │     (no uplink)
                     │             │     │             │ │             │
                     │ Event: 0x01 │     │ Event: 0x02 │ │ Event: 0x03 │
                     │ +NDEF ID    │     │ +NDEF ID    │ │ +NDEF ID    │
                     │             │     │             │ │ +Button ID  │
                     │ EPD:        │     │ EPD:        │ │             │
                     │ "Cleaning   │     │ "Last       │ │ EPD:        │
                     │ in progress"│     │ Cleaned:    │ │ No change   │
                     │             │     │ [RTC time]" │ │             │
                     │ LED: 2s     │     │ LED: 2      │ │ LED: 3      │
                     │ solid       │     │ blinks      │ │ flashes     │
                     └──────┬──────┘     └──────┬──────┘ └──────┬──────┘
                            │                   │               │
                            ▼                   ▼               ▼
                     Back to NORMAL      Back to NORMAL  Back to NORMAL
```

---

## Potential Logical Issues / Questions

### Issue 1: Button 0 and 5 can't be "voted" via Registered Vote

In the current scheme:
- Button 0 = **always** means Check-in
- Button 5 = **always** means Check-out
- Only Buttons 1-4 are available for Registered Votes

**Is this a problem?**
- If your decal has Button 0 or Button 5 as feedback options (e.g., "Great service" on Button 0), a manager **cannot** do a Registered Vote for those.
- If Buttons 0 and 5 are typically negative feedback anyway, maybe this is fine?

**Alternative if this is a problem:** Different entry sequences:
- Button 0+5 hold = Check-in/Check-out mode (scan → auto determines in vs out based on state)
- Button 2+3 hold = Registered Vote mode (scan → any button 0-5 is a vote)

### Issue 2: How does employee know which button to press?

Currently relies on:
- Training: "Button 0 = IN, Button 5 = OUT"
- Possibly a small employee-facing label near buttons

**Is this sufficient?** Or should there be an EPD prompt (Variant A only) like:
```
┌──────────────────────┐
│  TAP CARD TO:        │
│  [0] Check IN        │
│  [5] Check OUT       │
│  [1-4] Report Issue  │
└──────────────────────┘
```

### Issue 3: What if employee presses wrong button?

- Presses Button 0 (check-in) when they meant to check-out
- **Current behavior:** System uplinks check-in, EPD shows "Cleaning in progress"
- **Recovery:** Employee must redo the flow and press Button 5

No undo mechanism. Backend would see: Check-in → Check-out in quick succession. Is this acceptable?

---

## Questions for You

1. **Button 0/5 reserved** - Is it okay that Buttons 0 and 5 cannot be used for Registered Votes? Or do managers need to vote on ALL 6 buttons?

2. **EPD prompt during NFC flow** - Should EPD show instructions after card scan ("Press 0 for IN, 5 for OUT")? Or rely purely on training + LED?

3. **Wrong button recovery** - Is "just redo the flow" acceptable for mistakes?

---

**User**

ok so the way the button decals are designed are as follows:

the first button is "green" -- happy
second button is "yellow" -- satisfied

the next 4 buttons are "red" and then have a decal on it that tells what was the issue. now these decals on top change according to the area of deployment.

so i think we should make button 1 (green) as check in, button 2 (yellow) as checkout. and then logically the manager will only feel the need to register the vote based on the red buttons (3-6) because they will tell what the issue is.

also given this state perhaps what we can do is: the button press itself starts the nfc scan and also tells what button was presed?

so then, the user (custodian or magnager) first puts sytem in "i am going to either do checkin/checkout or registed vote state" suggest a better name for this state 

then lets say its a custodian, then once the led confirms that ok youre in elevated statge, they will first press button 1 (green) and put nfc card, then led will show if it was successfull or not,
same way they wil lrepeat for checkout (yellow button)

same for manager, if they have prolem on button 6, they will click it, nfc will accept scan and send uplink accordingly

bascally the buttons will invoke the nfc engine and at the same time will be packed into uplink payload . how does this sound?

---

**Cursor**

This is a much cleaner design! The button press determining the action *before* NFC scan makes more intuitive sense. Let me reformulate the flow.

---

## Revised Button Layout Understanding

| Button Index | Color | Normal Mode (Public) | Staff Mode (Employee) |
|--------------|-------|----------------------|----------------------|
| 0 | Green | Happy feedback | **Check-in** |
| 1 | Yellow | Satisfied feedback | **Check-out** |
| 2 | Red | Issue (per decal) | Registered Vote |
| 3 | Red | Issue (per decal) | Registered Vote |
| 4 | Red | Issue (per decal) | Registered Vote |
| 5 | Red | Issue (per decal) | Registered Vote |

---

## Name Suggestions for Elevated State

| Option | Pros | Cons |
|--------|------|------|
| **Staff Mode** | Simple, clear who it's for | - |
| **Service Mode** | Implies maintenance/service | Could be confused with device servicing |
| **Employee Mode** | Very explicit | Longer |
| **Auth Mode** | Short, implies authentication | Less clear to non-tech |
| **Credential Mode** | Describes what happens (NFC) | Technical |

**My recommendation: "Staff Mode"** - short, clear, and describes who uses it.

---

## Revised Flow: Staff Mode

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           NORMAL MODE                                    │
│                          (Public facing)                                 │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  • Button 0-5 press → LED 1s → Uplink (Event 0x00 + Button ID)          │
│  • 5-second lockout after each press                                    │
│  • EPD shows "Thank you" briefly, returns to "Last Cleaned"             │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
                                       │
                                       │ Button 0+5 hold 2s (or other combo)
                                       ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           STAFF MODE                                     │
│                      (Waiting for button selection)                      │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  LED: Distinct pattern to show "Staff Mode active"                      │
│  Timeout: 10 seconds → return to Normal Mode                            │
│                                                                         │
│  Employee presses a button (0-5):                                       │
│  • Button selection is captured                                         │
│  • NFC scan engine activates                                            │
│  • Transition to NFC_SCAN state                                         │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
                                       │
                                       │ Button 0-5 pressed
                                       ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         NFC_SCAN STATE                                   │
│                    (Button already selected)                             │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  LED: Slow blink 1Hz (waiting for card)                                 │
│  Timeout: 5 seconds → return to Normal Mode (no uplink)                 │
│                                                                         │
│  Employee taps card:                                                    │
│  • Read NDEF ID                                                         │
│  • If success → process action based on button                          │
│  • If fail → LED error pattern → return to Normal Mode                  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
                                       │
                                       │ Card read successful
                                       ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        ACTION EXECUTION                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Based on button pressed:                                               │
│                                                                         │
│  BUTTON 0 (Green) = CHECK-IN                                            │
│  • Uplink: Event 0x01 + Timestamp + NDEF ID                             │
│  • EPD: "Cleaning in progress"                                          │
│  • LED: Solid 2 seconds                                                 │
│                                                                         │
│  BUTTON 1 (Yellow) = CHECK-OUT                                          │
│  • Uplink: Event 0x02 + Timestamp + NDEF ID                             │
│  • EPD: "Last Cleaned: [RTC time]"                                      │
│  • LED: 2 blinks                                                        │
│                                                                         │
│  BUTTON 2-5 (Red) = REGISTERED VOTE                                     │
│  • Uplink: Event 0x03 + Timestamp + NDEF ID + Button ID                 │
│  • EPD: No change                                                       │
│  • LED: 3 quick flashes                                                 │
│                                                                         │
│  → Return to Normal Mode                                                │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Scenario Walkthroughs (Revised)

### Custodian Check-in

```
1. Custodian arrives at bathroom
2. Holds Button 0+5 for 2 seconds
   └─► LED: [Staff Mode indicator - e.g., 2 slow blinks]
   └─► System enters STAFF MODE
   
3. Custodian presses Button 0 (Green = Check-in)
   └─► LED: Slow blink 1Hz (NFC waiting)
   └─► NFC engine activated
   
4. Custodian taps their card
   └─► Card read success
   └─► LED: Solid 2 seconds (check-in confirmed)
   └─► EPD: "Cleaning in progress"
   └─► Uplink: Event 0x01 + Timestamp + NDEF ID
   
5. System returns to Normal Mode
```

### Custodian Check-out

```
1. Custodian finishes cleaning
2. Holds Button 0+5 for 2 seconds
   └─► LED: Staff Mode indicator
   
3. Custodian presses Button 1 (Yellow = Check-out)
   └─► LED: Slow blink 1Hz
   
4. Custodian taps their card
   └─► LED: 2 blinks (check-out confirmed)
   └─► EPD: "Last Cleaned: 14:35"
   └─► Uplink: Event 0x02 + Timestamp + NDEF ID
   
5. System returns to Normal Mode
```

### Manager Registered Vote

```
1. Manager notices smell issue (Button 4 per decal)
2. Holds Button 0+5 for 2 seconds
   └─► LED: Staff Mode indicator
   
3. Manager presses Button 4 (Red = Smell issue)
   └─► LED: Slow blink 1Hz
   
4. Manager taps their card
   └─► LED: 3 quick flashes (registered vote confirmed)
   └─► EPD: No change
   └─► Uplink: Event 0x03 + Timestamp + NDEF ID + Button 4
   
5. System returns to Normal Mode
```

---

## Updated State Machine

```
                              ┌─────────────────┐
                              │  NORMAL MODE    │
                              │  LED: Off       │
                              └────────┬────────┘
                                       │
         ┌─────────────────────────────┼─────────────────────────────┐
         │                             │                             │
         ▼                             ▼                             ▼
  ┌─────────────┐            ┌──────────────────┐           ┌─────────────┐
  │ Button 0-5  │            │ Button 0+5       │           │ Button      │
  │ (single)    │            │ hold 2s          │           │ 0+1+5 hold  │
  │             │            │                  │           │ 3s          │
  └──────┬──────┘            └────────┬─────────┘           └──────┬──────┘
         │                            │                            │
         ▼                            ▼                            ▼
  ┌─────────────┐            ┌──────────────────┐           ┌─────────────┐
  │ PUBLIC VOTE │            │   STAFF MODE     │           │ DEVICE INFO │
  │ LED: 1s     │            │ LED: 2 slow blink│           │ EPD: Info   │
  │ Uplink 0x00 │            │ Timeout: 10s     │           │             │
  │ +Button ID  │            └────────┬─────────┘           └─────────────┘
  └──────┬──────┘                     │
         │            ┌───────────────┼───────────────┐
         ▼            │               │               │
  Back to NORMAL      ▼               ▼               ▼
                 ┌─────────┐    ┌───────────┐   ┌─────────┐
                 │ TIMEOUT │    │ Button    │   │ Button  │
                 │ (10s)   │    │ 0+5 again │   │ 0-5     │
                 └────┬────┘    │ (cancel)  │   │ pressed │
                      │         └─────┬─────┘   └────┬────┘
                      ▼               ▼              │
                Back to NORMAL  Back to NORMAL       │
                                                     ▼
                                            ┌──────────────────┐
                                            │   NFC_SCAN       │
                                            │ LED: Slow 1Hz    │
                                            │ Button stored    │
                                            │ Timeout: 5s      │
                                            └────────┬─────────┘
                                                     │
                                  ┌──────────────────┼──────────────────┐
                                  │                  │                  │
                                  ▼                  ▼                  ▼
                           ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
                           │  TIMEOUT    │    │  READ FAIL  │    │  READ OK    │
                           │  (5s)       │    │ LED: 3 fast │    │             │
                           └──────┬──────┘    └──────┬──────┘    └──────┬──────┘
                                  │                  │                  │
                                  ▼                  ▼                  ▼
                           Back to NORMAL     Back to NORMAL    ┌─────────────────┐
                                                                │ PROCESS ACTION  │
                                                                │                 │
                                                                │ If Btn 0:       │
                                                                │  Event 0x01     │
                                                                │  EPD: Cleaning  │
                                                                │  LED: 2s solid  │
                                                                │                 │
                                                                │ If Btn 1:       │
                                                                │  Event 0x02     │
                                                                │  EPD: Last Clean│
                                                                │  LED: 2 blinks  │
                                                                │                 │
                                                                │ If Btn 2-5:     │
                                                                │  Event 0x03     │
                                                                │  +Button ID     │
                                                                │  LED: 3 flash   │
                                                                └────────┬────────┘
                                                                         │
                                                                         ▼
                                                                  Back to NORMAL
```

---

## Updated LED UI for Staff Mode

| State | LED Pattern | Duration |
|-------|-------------|----------|
| **Staff Mode Entry** | 2 slow blinks | 1s on, 500ms off, 1s on |
| **Staff Mode Active** | Solid dim / very slow pulse | Until button or timeout |
| **NFC Waiting** | Slow blink 1Hz | Until scan or timeout |
| **NFC Read Fail** | 3 fast blinks | 600ms total |
| **Check-in Confirmed** | Solid 2s | 2000ms |
| **Check-out Confirmed** | 2 blinks | 1s on, 200ms off, 800ms on |
| **Registered Vote Confirmed** | 3 quick flashes | 450ms total |

---

## Benefits of This Revised Flow

1. **Button-first intent** - Employee declares what they want to do BEFORE scanning (more intuitive)
2. **Green/Yellow mapping** - Natural: Green = start (check-in), Yellow = done (check-out)
3. **All 6 buttons usable for registered vote** - Buttons 2-5 for issues, and technically 0-1 could be used for "positive" registered votes if needed
4. **Simpler NFC state** - No "awaiting button" state after scan; action is already known
5. **Cancel option** - Press 0+5 again during Staff Mode to cancel (optional)

---

## Questions

1. **Staff Mode entry combo** - Still okay with Button 0+5 hold 2s? Or prefer different combo now that 0 and 1 have specific meanings in Staff Mode?

2. **Staff Mode timeout** - 10 seconds waiting for button press before returning to Normal Mode - acceptable?

3. **Cancel mechanism** - If employee enters Staff Mode accidentally, should pressing 0+5 again cancel? Or just wait for timeout?

4. **Staff Mode active indicator** - While waiting for button press in Staff Mode, should LED be:
   - A) Solid on (simple)
   - B) Very slow pulse (distinguishable from other states)
   - C) Off (rely on timeout)

---

**User**

lets do button 0+1 to enter "Staff Mode". 
yes lets use timer based timeouts wherever possible to get back to prev/next logical statemachine whenever possible.
i dont mind about led, lets do whatver is easy.

---

**Cursor**

Got it. Let me finalize the state machine with Button 0+1 for Staff Mode entry.

---

## Final State Machine Summary

### Entry Combos

| Combo | Hold Duration | Action |
|-------|---------------|--------|
| Button 0+1 | 2 seconds | Enter **Staff Mode** |
| Button 0+1+5 | 3 seconds | Enter **Device Info Mode** |
| All 6 buttons | 10 seconds | **Reboot** |

### States & Timeouts

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              NORMAL MODE                                     │
│  • LED: Off                                                                  │
│  • Button 0-5 single press → Public vote → LED 1s → Uplink 0x00             │
│  • 5-second lockout between presses                                         │
└─────────────────────────────────────────────────────────────────────────────┘
         │                              │                              │
         │ Btn 0+1 hold 2s              │ Btn 0+1+5 hold 3s            │ All 6 hold 10s
         ▼                              ▼                              ▼
┌─────────────────┐            ┌─────────────────┐            ┌─────────────────┐
│   STAFF MODE    │            │  DEVICE INFO    │            │     REBOOT      │
│                 │            │                 │            │                 │
│ LED: Solid on   │            │ EPD: Show info  │            │ LED: Solid 3s   │
│ Timeout: 10s    │            │ Timeout: 30s    │            │ Then reboot     │
│                 │            │ Any btn → exit  │            │                 │
│ Btn 0-5 pressed │            └────────┬────────┘            └─────────────────┘
│ → NFC_SCAN      │                     │
└────────┬────────┘                     ▼
         │                       Back to NORMAL
         │ Button 0-5 pressed
         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              NFC_SCAN                                        │
│  • LED: Slow blink (1 Hz)                                                   │
│  • Button ID stored (determines action on success)                          │
│  • Timeout: 5 seconds → Back to NORMAL (no uplink)                          │
│  • Card read fail → LED 3 fast blinks → Back to NORMAL                      │
│  • Card read success → PROCESS ACTION                                       │
└─────────────────────────────────────────────────────────────────────────────┘
         │
         │ Card read success
         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            PROCESS ACTION                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Button 0 (Green) → CHECK-IN                                                │
│  • Uplink: Event 0x01 + Timestamp + NDEF ID                                 │
│  • EPD: "Cleaning in progress"                                              │
│  • LED: Solid 2 seconds                                                     │
│                                                                             │
│  Button 1 (Yellow) → CHECK-OUT                                              │
│  • Uplink: Event 0x02 + Timestamp + NDEF ID                                 │
│  • EPD: "Last Cleaned: [RTC time]"                                          │
│  • LED: 2 blinks                                                            │
│                                                                             │
│  Button 2-5 (Red) → REGISTERED VOTE                                         │
│  • Uplink: Event 0x03 + Timestamp + NDEF ID + Button ID                     │
│  • EPD: No change                                                           │
│  • LED: 3 quick flashes                                                     │
│                                                                             │
│  → Back to NORMAL                                                           │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Complete Timeout Summary

| State | Timeout | On Timeout |
|-------|---------|------------|
| Normal Mode | None | - |
| Staff Mode | 10 seconds | Back to Normal (no uplink) |
| NFC Scan | 5 seconds | Back to Normal (no uplink) |
| Device Info | 30 seconds | Back to Normal |
| Button lockout (public) | 5 seconds | Accept next press |
| "Thank you" on EPD | 5 seconds | Return to "Last Cleaned" |

---

## Final LED Patterns (Simplified)

| State | LED | Notes |
|-------|-----|-------|
| Power On | 2 flashes | Boot confirmation |
| Join Success | 3 flashes | Network connected |
| Normal/Idle | Off | Save power |
| Button Accepted (public) | Solid 1s | Vote confirmed |
| Button Rejected (lockout) | Off | Silent rejection |
| Staff Mode Active | Solid on | Waiting for button |
| NFC Waiting for Card | Blink 1Hz | Waiting indicator |
| NFC Read Fail | 3 fast blinks | Error |
| Check-in Confirmed | Solid 2s | Single long = IN |
| Check-out Confirmed | 2 blinks | Double = OUT |
| Registered Vote Confirmed | 3 flashes | Triple = vote |
| Reboot Triggered | Solid 3s → off | Confirm then reboot |

---

## Quick Reference: All Button Combos

| Combo | Hold | Action |
|-------|------|--------|
| Single button (0-5) | Tap | Public feedback vote |
| Button 0+1 | 2s | Enter Staff Mode |
| Button 0+1+5 | 3s | Device Info display |
| All 6 buttons | 10s | Reboot device |

---

This locks down the core interaction model. 

**What's left to finalize?**

1. **Uplink payload schemas** - We discussed these earlier but should confirm final byte layouts
2. **Downlink commands** - Confirmed 4 commands, need final byte encoding
3. **EPD content details** - Exact text/layout for each state
4. **Boot sequence** - Confirmed earlier, just needs documentation
5. **Housekeeping tasks** - Daily heartbeat activities
6. **Customer validation items** - Questions still pending customer input

Would you like to:
- A) Lock down payload schemas (uplink/downlink byte formats)
- B) Detail the EPD screens/layouts
- C) Review the full requirements document draft
- D) Something else

