# LoRaWAN device firmware requirements
_Exported on 1/22/2026 at 11:53:05 EST from Cursor (2.4.7)_

---

**User**

You are an embedded systems requirements engineer specializing in IoT firmware development. I'm developing firmware for a battery-operated LoRaWAN device with the following hardware:
Hardware Components:

NRF52840 microcontroller running Zephyr OS
6 physical buttons + 1 reed switch button
PN5180 NFC module
SX1262 LoRa module
E-Paper display
Realtime clock
SDHC card slot
EEPROM
1 LED
Battery with ADC monitoring pin

High-Level Concept:
Users press the 6 buttons to generate data that gets uplinked via LoRa. The NFC module handles check-in/check-out events with timestamps displayed on the e-paper screen.
Your Role:
Help me define complete functional requirements for a customer sign-off document. Use a conversational approach to guide me through requirements discovery.
Process - Two Phase Questioning:
Phase 1 - Breadth First (Subsystem Overview):
Start by asking broad questions to understand how each subsystem needs to work:

What is the primary use case and who are the end users?
How do the 6 buttons function in the user workflow?
What is the NFC check-in/check-out workflow?
What triggers LoRa uplinks and what data gets sent?
What information does the e-paper display show?
How does the reed switch fit into the system?
What role do the RTC, SDHC, and EEPROM play?
What's the battery life expectation?
What features or functionality are explicitly OUT OF SCOPE?

Phase 2 - Depth Per Module:
After understanding the big picture, dive deep into each peripheral/subsystem:

Explore edge cases I haven't considered
Challenge assumptions
Ask about error handling and failure modes
Probe power management implications
Question data persistence needs
Examine user feedback mechanisms
Consider maintenance and diagnostics

Throughout the conversation:

Maintain a running ASSUMPTIONS LIST of things that need customer validation
Focus on WHAT should happen (requirements), not HOW (implementation)
Ask clarifying questions when my answers are ambiguous

Final Deliverable:
Create a requirements document with:

Functional Description - Broad overview of system behavior
Functional Requirements - Traditional "shall" statements organized by:

Overall system behavior
Per-peripheral requirements (Buttons, NFC, LoRa, Display, RTC, Storage, Power, LED)


Assumptions List - Items requiring customer validation

Begin by asking your Phase 1 breadth-first questions to map out the system.

---

**Cursor**

# Requirements Discovery Session - Battery-Operated LoRaWAN Device

Excellent! I'm ready to help you build a comprehensive functional requirements document for customer sign-off. Let's work through this systematically.

---

## Phase 1: Breadth-First Discovery

I'll ask these questions in logical groupings so we can efficiently map out your system. Please provide as much detail as you can—even rough ideas help me understand the intent.

---

### Group A: Use Case & User Context

1. **What is the primary use case and who are the end users?**
   - What problem does this device solve?
   - Who physically interacts with it (employees, customers, field technicians)?
   - What is the deployment environment (indoor, outdoor, industrial, retail)?

2. **What features or functionality are explicitly OUT OF SCOPE for v1.1?**
   - Are there features you've discussed but decided to defer?
   - Any hardware capabilities you intentionally won't use?

---

### Group B: Core Workflow - Buttons & Data Generation

3. **How do the 6 buttons function in the user workflow?**
   - Are they labeled/dedicated functions, or configurable?
   - Single press vs. long press vs. combinations?
   - What "data" does each button press generate?
   - Is there a sequence or can buttons be pressed independently?

4. **How does the reed switch (7th button) fit into the system?**
   - What triggers it (door open/close, magnet proximity)?
   - Does it behave differently from the 6 physical buttons?
   - Is it for tamper detection, enclosure state, or user interaction?

---

### Group C: NFC Check-In/Check-Out Workflow

5. **What is the NFC check-in/check-out workflow?**
   - What NFC tags/cards are being read (employee badges, asset tags)?
   - What constitutes a "check-in" vs. "check-out" event?
   - Is this time-based, location-based, or explicit user action?
   - Does the device write to NFC tags, or read-only?

---

### Group D: Communication & Display

6. **What triggers LoRa uplinks and what data gets sent?**
   - Button presses, NFC events, periodic heartbeats, or all of the above?
   - What is the payload structure (button ID + timestamp, NFC UID, etc.)?
   - Is there any downlink functionality (commands from server)?
   - What LoRaWAN class (A, B, or C)?

7. **What information does the e-paper display show?**
   - Check-in/check-out confirmation with timestamp?
   - Device status (battery, connectivity)?
   - User instructions or prompts?
   - Does it update only on events, or periodically?

---

### Group E: Supporting Subsystems

8. **What role do the RTC, SDHC, and EEPROM play?**
   - **RTC**: Just timestamps, or scheduled wake-ups/alarms?
   - **SDHC**: Local data logging, firmware updates, configuration files?
   - **EEPROM**: Device configuration, counters, credentials?

---

### Group F: Power & Constraints

9. **What's the battery life expectation?**
   - Target operational lifetime (weeks, months, years)?
   - Expected usage intensity (events per day)?
   - Is there a low-battery warning requirement?
   - Replaceable or rechargeable battery?

---

**Please answer these questions in whatever order and detail makes sense to you.** Feel free to say "I don't know yet" or "needs customer input" for uncertain items—I'll add those to the Assumptions List.

Once I understand the big picture, we'll move to Phase 2 where I'll challenge edge cases, probe failure modes, and help you think through scenarios you may not have considered yet.

---

**User**

Group A: Use Case & User Context
What is the primary use case and who are the end users?
-- Core idea: a box that 6 buttons, 1 epd, that users can just do single press to tell their feedback based on a usecase.
- the system in essence captures user votes. the decal on the system/and backend (which we are not concerend with) attaches meaning to the buttons.
- For example, the decal can say buttons 1-3 mean good service in the bathroom, 4-6 mean there were problem with cleaning/odor/maintence etc.

What problem does this device solve?
- captures user sentiments

Who physically interacts with it (employees, customers, field technicians)?
- footraffic people (think people who just got coffee and want to give feedback, people in bathrooms, people in concerts, airport secruity checkout)
- employees will also be able to intreact with the system where with the nfc module they will be able to 'check in/checkout' , and send a "registed vote" (not sure how this will happen, we need to think about it)

What is the deployment environment (indoor, outdoor, industrial, retail)?
- indoor

What features or functionality are explicitly OUT OF SCOPE for v1.1?
- no ota, no app etc. no complex gui on epd

Are there features you've discussed but decided to defer?
- no we need to lay them out rn

Any hardware capabilities you intentionally won't use?
- no all the peripherals are here for a reason



----


Group B: Core Workflow - Buttons & Data Generation
How do the 6 buttons function in the user workflow?
Are they labeled/dedicated functions, or configurable?
Single press vs. long press vs. combinations?
What "data" does each button press generate?
Is there a sequence or can buttons be pressed independently?

-- from customer pov they will just do a single press that will light up one led
-- they will be labled from decal, and or the meaning will be given at the backend (not our job)
-- i have already scoped out a schema for on button press. basically it will prep a payaldo that will ahve timestamp, button id (0-6), running count of that button til date
-- no sequence, the customer walks in and just ideally presses one button to tell what their feedback was. after that button press the system wont accept another button press for say 5 seconds so that people dont spam it.
-- From firware, we just uplink the button id (0-6) 

How does the reed switch (7th button) fit into the system?
What triggers it (door open/close, magnet proximity)?
Does it behave differently from the 6 physical buttons?
Is it for tamper detection, enclosure state, or user interaction?

-- lets scrap the reed switch. it was orignally thought to be used to invoke the nfc scan logic, but now we will be using our 6 buttons and some statemachie ui (not sure what and how) that only the emplyoyees will no and then will be able ot start the nfc scan routine. lets circle back later when we reach there. im not sure how exactly that will happen.



Group C: NFC Check-In/Check-Out Workflow
What is the NFC check-in/check-out workflow?
What NFC tags/cards are being read (employee badges, asset tags)?
What constitutes a "check-in" vs. "check-out" event?
Is this time-based, location-based, or explicit user action?
Does the device write to NFC tags, or read-only?

-- im a little uncelar and bascially we need to properly scope out the edge caes and get bac kto the customer.
-- but the basic idea is this
-- this unit we are making is going to be deployed at bathrooms
-- so this is how it will go, say a custodian comes in to cleanm the bathroom and somehow will use the nfc module to tap their iso15693 card, the system will ONLY read the UUID, and uplink that data (with that scehma). say they press the button in some order that starts the nfc scan, and then goes about cleaning 
-- now after they are done cleaning, they will again do some button press in an order and scan the card that will tell my system that ok this is now a checkout (it will again uplionk that uuid) and now it will tkae the timestamp from rtc and then display that timestamp on the epd
-- another use case that was floated was that say a manger comens in the bathroom. they see that ok there is proably a problem with smell (say button 4) then they will "check in" with their iso15693 tag and then press button 4 (what we call a registed vote) that way the backend will now that with that uuid combo and button4 that bathroom has had problmes with smell. 
-- since most of the logic is done on backend, we just need to appaorapoirately send the uplinks that has enoguh data for them to then compute and add logic on the uplinks.


Group D: Communication & Display
What triggers LoRa uplinks and what data gets sent?
Button presses, NFC events, periodic heartbeats, or all of the above?
What is the payload structure (button ID + timestamp, NFC UID, etc.)?
Is there any downlink functionality (commands from server)?
What LoRaWAN class (A, B, or C)?

-- main hero is button press (from customer pov, because thats the main product, getting customer feedback)
-- yes we would like to have periodic heartbeats that will send idk like battery level, some other critrical info to backend to guaage state of device (not sure, and also lets not complicate this)
-- yes i have a shecham that i am thining 
Uplink Payload Schema	 Max User Payload in US915 DR0 - 11bytes										
											
Byte #	0	1	2	3	4	5	6	7	8	9	10
Schema	UTC Timestamp (Fixed)				Event Type (Fixed)	Variable (as dictated by Event Type)					
Range (Possible Values)					256						
					0x00 - Button Press						
					0x01 - NFC Tag						
					0x02 - Battery %						
					.						
					.						
					.						
					0xFF - Future Event Type Additions						
											
											
											
											
Example Payloads											
1. On Button Press	0x69	0x38	0x6F	0x33	0x00	0x00 - 0x06	0x01	0xE2	0x40	NULL	
Explanation	UTC timestamp "1765306163" in Hex				Button Press: Event Type 0x00	Button ID # Range	Running Count of Button ID # (Counter Range 0-16777216)				
Final Sample Payload	69386F33000001E240										
											
2. On NFC Scan	0x69	0x38	0x6F	0x33	0x01	0x01	0x50	0xB5	0x94	0x34	0xA3
Explanation	UTC timestamp "1765306163" in Hex				NFC Read: Event Type 0x01	NFC Tag UID					
Final Sample Payload	69386F33010150B59434A3										

This is a good starting point, and i based it on thining that worst case my DR0 will always be so lets try pack in 11bytes. but based on what we plan over here it will change, we can talk about that later,. currently we interested in what the system will do and not HOW. IF I SWAY TOO MYUCH BRING IT BACK TO our core goal and so i dont get lost in the how and thinking too myuch about firmware



Is there any downlink functionality (commands from server)?
-- yes there has to be downlink fucntioantly abosulety. currentyl threre are 2 thinks i can envvioiopn
-- some clients have thier own way to showcase the last cleaned time that they will send to the backned and then the backend will queue it for disply on epd. now im not sure how this will happen cz my devices are going to be Class A, and idk when will we open up the rx channels. I need answer these questions cz the client thinks of downlink disaply update as ondemand /realtime service, but thats not gonna happen so we nned to chalk out whats realsitic and best scneario
-- yes downlinks will also be used to do some kind of query / minimal config fucntions. one requirement is that a downlink message should be able to full referesh, the epd. im not sure what else we can propose for downlink querry/configs, but shoots some ideas based on all the things our systems does if you have any inputs that can be good value additon

What LoRaWAN class (A, B, or C)?
yes class A 


What information does the e-paper display show?
Check-in/check-out confirmation with timestamp?
Device status (battery, connectivity)?
User instructions or prompts?
Does it update only on events, or periodically?


-- most of the time it has to show "LAST CLEANED AT:" heading and then a timestamp below it which will keep gettign updating
-- no device status, epd is more of client facing thing
--- after a button register from customer, it will repalce the last cleaned, show "Thank you for your feedback" and then go back to last cleaned after sometime>
-- im maybe thinking that when the custodian is cleaneing, it maybe shows "cleaning in progress"
-- yes will updateo on btton press event. 


Group E: Supporting Subsystems
What role do the RTC, SDHC, and EEPROM play?
RTC: Just timestamps, or scheduled wake-ups/alarms?
SDHC: Local data logging, firmware updates, configuration files?
EEPROM: Device configuration, counters, credentials?

yes maybe rtc will be used for wakeups and alarams im not sure atm. 
sdhc will ideally generate some kind of log file that wil lstore info about that day (maybe eod) say the runnign count of each button (this info is crucial since the product is a data colelcting device at the end, tells what feedaback peple gave) and some other things i haven;'t thgouth. no firmware updates, no configs files
eeprom: im thinking eeprom will be a intermediate way to store the button cpounters on press so that i dont do repitative read/write on sdhc, we then trasnfer it out to sdhc sometime. opther thing im thinking is using eeprom to main devnoncees so that we dont have colliosn etc and it can count up to ensure we always join nicely


What's the battery life expectation?
Target operational lifetime (weeks, months, years)?
i odnt have the answer, but we need to maximze and use sleep / deepsleep modes etc when it comes tofirmware, 8-10 months is good
Expected usage intensity (events per day)?
Based on the assumption that we see around 10 presses/hr and we're storing data for 2 years:  We expect 175,200 presses in 2 years. 24bit counter should be plenty
Is there a low-battery warning requirement?
havent thought but yes, when the system sampels adc and reaches below threashould it will uplink a pseicf data to backend that should send some allarmas
Replaceable or rechargeable battery?
repalceleble 1-2 9600mah battery






---

**Cursor**

# Phase 1 Summary & Transition to Phase 2

Excellent responses! I now have a solid picture of the system. Before diving deep into each module, let me capture what I've understood and flag some immediate ambiguities.

---

## Current Understanding

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        FEEDBACK DEVICE v1.1                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│  PRIMARY USERS:                                                             │
│  • Public foot traffic → Single button press for feedback                   │
│  • Employees (custodians/managers) → NFC check-in/out + registered votes    │
│                                                                             │
│  CORE FLOWS:                                                                │
│  1. Customer Feedback: Button press → LED flash → 5s lockout → LoRa uplink  │
│  2. Custodian Check-in: Secret sequence → NFC scan → Uplink UUID            │
│  3. Custodian Check-out: Secret sequence → NFC scan → Uplink + EPD update   │
│  4. Registered Vote: Check-in → Button press → Associates UUID + Button     │
│  5. Heartbeat: Periodic battery/status uplink                               │
│                                                                             │
│  EPD STATES:                                                                │
│  • Default: "LAST CLEANED AT: [timestamp]"                                  │
│  • Post-feedback: "Thank you for your feedback" → return to default         │
│  • During cleaning: "Cleaning in progress"                                  │
│                                                                             │
│  DOWNLINKS (Class A constraints apply):                                     │
│  • EPD content update (timestamp from backend)                              │
│  • Full EPD refresh command                                                 │
│  • Configuration/query commands (TBD)                                       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## ASSUMPTIONS LIST (Running - Needs Customer Validation)

| # | Assumption | Status |
|---|------------|--------|
| A1 | Button IDs are 0-5 (6 buttons total), not 0-6 | **CLARIFY** |
| A2 | 5-second lockout applies only to public users, not employee NFC workflow | **CLARIFY** |
| A3 | ISO 15693 tags have 8-byte UIDs (your schema shows 6 bytes for NFC UID) | **CLARIFY** |
| A4 | "Last Cleaned" timestamp comes from local RTC at check-out, not from backend | **CLARIFY** |
| A5 | Button running counts persist across battery replacement | **CLARIFY** |
| A6 | Reed switch hardware exists but will not be used in v1.1 firmware | Noted |
| A7 | Indoor deployment = no weatherproofing/temp range requirements | Assumed |
| A8 | Single LED for all feedback (not per-button LEDs) | **CLARIFY** |

---

## Phase 2: Deep Dive Questions

Let me now probe the critical ambiguities and edge cases. I'll tackle the **most uncertain areas first**.

---

### 2A: NFC Workflow - This Needs Definition

This is the murkiest area. Let me ask pointed questions:

**Entry into NFC Mode:**
1. You mentioned "button sequence" to enter NFC scan mode. What sequence are you envisioning?
   - Example options: Long-press Button 1? Press Button 1+6 simultaneously? Press 1-2-3 in order?
   - How long does the device wait for a card tap after entering NFC mode?
   - What if no card is tapped (timeout)?

**Check-in vs Check-out Differentiation:**
2. How does the system distinguish check-in from check-out?
   - Option A: Different button sequences for each (e.g., 1-2-3 = check-in, 3-2-1 = check-out)
   - Option B: Toggle mode - first scan = check-in, second scan from same UID = check-out
   - Option C: System tracks "open sessions" - if UUID has no check-out, next scan is check-out
   - Which approach, or something else?

**Registered Vote Flow:**
3. For the manager "registered vote" scenario:
   - Does the manager first check-in (NFC scan), THEN press a button?
   - Or is it: enter NFC mode → scan card → then the next button press within X seconds is "registered"?
   - Is this a single combined uplink (UUID + Button ID) or two separate uplinks?

**Edge Cases:**
4. What if a custodian checks in but never checks out (forgets, emergency, etc.)?
5. What if two different employees scan during overlapping "sessions"?
6. Can a manager do a "registered vote" without doing a full check-in/check-out cycle?

---

### 2B: EPD State Machine & Timing

1. After "Thank you for your feedback" displays, how long before returning to "Last Cleaned"? (5 seconds? 10?)
2. When "Cleaning in progress" is shown, does it:
   - Show immediately on check-in?
   - Block customer button presses during cleaning?
3. If a downlink updates the EPD with a new "Last Cleaned" time, does this override the local RTC-generated timestamp?
4. What is the fallback if RTC loses time (battery disconnect, RTC failure)?

---

### 2C: Button Behavior & LED

1. **Button ID numbering**: Is it 0-5 or 1-6? Your schema says 0-6 which implies 7 values.
2. **LED behavior on button press**: 
   - How long does it stay on? (500ms? 1 second?)
   - Same LED for all buttons, or are there 6 LEDs?
   - Any different LED pattern for "button rejected" (during 5s lockout)?
3. **5-second lockout**: 
   - Does it apply per-button or globally (no buttons work for 5s)?
   - Visual/audio feedback that press was ignored?

---

### 2D: LoRa & Downlink Reality Check

**Class A Constraint Education for Customer:**
Class A devices only open RX windows for ~1-2 seconds after each uplink. This means:
- Downlinks can ONLY be delivered after the device uplinks
- If no uplinks happen, no downlinks can be received
- "On-demand" EPD updates from backend are **not real-time** - they queue until next uplink

**Questions:**
1. Is the customer aware of this limitation? This is critical for expectation setting.
2. For "EPD update from backend" use case:
   - Option A: Backend queues message, device gets it after next button press uplink
   - Option B: Device sends periodic heartbeat (e.g., every 15 min), downlink delivered then
   - What latency is acceptable? Minutes? Hours?
3. **Heartbeat interval**: How often? (Impacts battery life significantly)
   - Every 15 minutes = ~96 uplinks/day
   - Every hour = ~24 uplinks/day
   - Every 6 hours = 4 uplinks/day

---

### 2E: Power & Storage

1. **Low battery threshold**: At what percentage should the warning uplink trigger? (10%? 15%?)
2. **Button counters on battery replacement**:
   - Should counters reset to zero, or must they persist?
   - If persist: EEPROM survives battery removal, but SDHC might need re-mount handling
3. **SDHC logging**: 
   - What format? Plain text CSV? Binary?
   - What happens when card is full?
   - Is the card ever removed for data extraction, or purely backup?

---

### 2F: Error Handling & Failure Modes

1. **LoRa join failure**: What if device can't join network on boot? Retry forever? Indicate on EPD?
2. **NFC read failure**: Card tap but UID read fails - retry? Feedback to user?
3. **EPD update failure**: E-paper partial refresh fails - does it retry? Show error state?
4. **RTC drift/failure**: How critical is timestamp accuracy? Seconds? Minutes?
5. **SDHC not present/failed**: Does device still operate? What's lost?

---

**Please answer what you can, and mark anything as "needs customer input" - I'll track it all in the Assumptions List. Focus especially on Section 2A (NFC Workflow) since that's the most undefined.**

```plaintext
┌─────────────────────────────────────────────────────────────────────────────┐
│                        FEEDBACK DEVICE v1.1                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│  PRIMARY USERS:                                                             │
│  • Public foot traffic → Single button press for feedback                   │
│  • Employees (custodians/managers) → NFC check-in/out + registered votes    │
│                                                                             │
│  CORE FLOWS:                                                                │
│  1. Customer Feedback: Button press → LED flash → 5s lockout → LoRa uplink  │
│  2. Custodian Check-in: Secret sequence → NFC scan → Uplink UUID            │
│  3. Custodian Check-out: Secret sequence → NFC scan → Uplink + EPD update   │
│  4. Registered Vote: Check-in → Button press → Associates UUID + Button     │
│  5. Heartbeat: Periodic battery/status uplink                               │
│                                                                             │
│  EPD STATES:                                                                │
│  • Default: "LAST CLEANED AT: [timestamp]"                                  │
│  • Post-feedback: "Thank you for your feedback" → return to default         │
│  • During cleaning: "Cleaning in progress"                                  │
│                                                                             │
│  DOWNLINKS (Class A constraints apply):                                     │
│  • EPD content update (timestamp from backend)                              │
│  • Full EPD refresh command                                                 │
│  • Configuration/query commands (TBD)                                       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

**User**

A1	Button IDs are 0-5 (6 buttons total), not 0-6	CLARIFY
-- yes there are 0-5 buttons, 6th button was connected with reed switch in orginal design that was meant to start nfc scan when the tag + attached with magnet would've been broguht close to it. however no we will be moving to some button sequence to initate it., 

A2	5-second lockout applies only to public users, not employee NFC workflow	CLARIFY
-- yes true, the employee might want register multiple votes (smell, cleanliness etc)

A3	ISO 15693 tags have 8-byte UIDs (your schema shows 6 bytes for NFC UID)	CLARIFY
-- yes so since i was limited to 11bytes where i considered DR0 to be always the worst case, and i dropped first 2 butes. Ik this will be a problem, I am thinking of reading user NDEF record and not UID to. In that case since i control the card issual id, ill bring it down to 4bytes ids issued sequential, (2^32 possbible cards) which is plenty. So lets say nfc schema is going to be timestamp + event type + button id (tells what button the manager pressed for registed vote) + 4 bytes of data i read + 1 null byte

A4	"Last Cleaned" timestamp comes from local RTC at check-out, not from backend	CLARIFY
-- Kind of. When checkin/out by custodian comes, it'll get timestamp from rtc. however there is an idea that backend will also supply, for customers that have their own webhooks and way of mainting last cleaned data. 

A5	Button running counts persist across battery replacement	CLARIFY
-- yes,

A6	Reed switch hardware exists but will not be used in v1.1 firmware	Noted
-- true

A7	Indoor deployment = no weatherproofing/temp range requirements	Assumed
-- true

A8	Single LED for all feedback (not per-button LEDs)	CLARIFY
-- yes single led will do all kind of user / customer / UI feedback



---

**Cursor**

# Assumptions Clarified - Updated List

Great, those clarifications help significantly. Let me update the assumptions list:

| # | Assumption | Status |
|---|------------|--------|
| A1 | Button IDs are 0-5 (6 buttons total) | ✅ Confirmed |
| A2 | 5-second lockout applies only to public users, not employee NFC workflow | ✅ Confirmed |
| A3 | NFC will read 4-byte NDEF user record (sequential IDs you control), not 8-byte UID | ✅ Confirmed |
| A4 | "Last Cleaned" timestamp: Local RTC on check-out, BUT backend can also push via downlink | ✅ Confirmed |
| A5 | Button running counts persist across battery replacement (stored in EEPROM → SDHC) | ✅ Confirmed |
| A6 | Reed switch hardware exists but unused in v1.1 | ✅ Confirmed |
| A7 | Indoor deployment only | ✅ Confirmed |
| A8 | Single LED for all UI feedback | ✅ Confirmed |

---

## Updated NFC Payload Schema

Based on A3 clarification:

```
NFC Event Uplink (11 bytes max):
┌──────────────────┬────────────┬───────────┬──────────────┬──────┐
│ UTC Timestamp    │ Event Type │ Button ID │ NDEF User ID │ NULL │
│ (4 bytes)        │ (1 byte)   │ (1 byte)  │ (4 bytes)    │ (1)  │
│ 0x69386F33       │ 0x01       │ 0x00-0x05 │ Sequential   │ 0x00 │
└──────────────────┴────────────┴───────────┴──────────────┴──────┘
```

**New Question on NFC Schema:**
- For a simple check-in (no registered vote), what goes in the Button ID byte? 
  - `0xFF` to indicate "no button / just check-in"?
  - Or do you need separate event types: `0x01` = NFC Check-in, `0x02` = NFC Check-out, `0x03` = Registered Vote?

---

## Continuing Phase 2 - Awaiting Your Answers

You've addressed the assumptions, but I still need answers to the **Phase 2 deep-dive questions** from my previous message. Let me re-present the critical ones:

---

### 2A: NFC Workflow Definition (CRITICAL - MOST UNDEFINED)

1. **Entry sequence**: What button sequence enters NFC scan mode? 
   - Long-press? Simultaneous press? Sequential combo?
   
2. **Check-in vs Check-out differentiation**: How does the system know which one?
   - Different sequences for each?
   - Toggle mode (1st scan = in, 2nd scan = out)?
   - State tracking per UUID?

3. **Registered Vote flow**: 
   - Is it: Enter NFC mode → Scan → Press button(s) → Exit?
   - Can employee press multiple buttons for one scan (smell + cleanliness)?
   - How does employee "exit" registered vote mode?

4. **Timeout**: How long does device wait for NFC tap after entering scan mode?

5. **Edge case**: Custodian checks in but never checks out - what happens?

---

### 2B: EPD Timing & States

1. "Thank you for feedback" → how many seconds before returning to "Last Cleaned"?
2. Does "Cleaning in progress" block customer button presses?
3. If backend downlink updates EPD, does it override local RTC timestamp?

---

### 2C: LED Behavior

1. LED on-time after button press? (500ms? 1s? 2s?)
2. Different LED pattern when button is rejected (during lockout)?
3. LED feedback during NFC scan mode? (blinking to indicate "waiting for card"?)

---

### 2D: LoRa Downlink & Heartbeat

1. **Heartbeat interval**: How often? (Impacts battery significantly)
   - Suggestion: Every 1-6 hours is reasonable for Class A battery device
   
2. **Downlink latency expectation**: Customer must understand EPD updates arrive only after next uplink. Is "within next heartbeat interval" acceptable?

3. **Proposed downlink commands** (for your consideration):
   - `0x01` - Update EPD "Last Cleaned" text (with timestamp payload)
   - `0x02` - Full EPD refresh (clear ghosting)
   - `0x03` - Request status report (device uplinks battery %, counters, etc.)
   - `0x04` - Reset button counters
   - `0x05` - Set heartbeat interval
   - Any others you need?

---

### 2E: Error Handling

1. **LoRa join failure on boot**: Retry forever? Show on EPD? Timeout?
2. **NFC read failure**: Feedback to employee?
3. **SDHC missing/full**: Does device still work?

---

**Please work through these - especially 2A (NFC workflow) since that's the core employee interaction that's still undefined.**

```plaintext
NFC Event Uplink (11 bytes max):
┌──────────────────┬────────────┬───────────┬──────────────┬──────┐
│ UTC Timestamp    │ Event Type │ Button ID │ NDEF User ID │ NULL │
│ (4 bytes)        │ (1 byte)   │ (1 byte)  │ (4 bytes)    │ (1)  │
│ 0x69386F33       │ 0x01       │ 0x00-0x05 │ Sequential   │ 0x00 │
└──────────────────┴────────────┴───────────┴──────────────┴──────┘
```

---

**User**

New Question on NFC Schema:
For a simple check-in (no registered vote), what goes in the Button ID byte?
0xFF to indicate "no button / just check-in"?
Or do you need separate event types: 0x01 = NFC Check-in, 0x02 = NFC Check-out, 0x03 = Registered Vote?

im inclined towards adding sepereate event types that way we are agnostic and just send data, the backend parses and adds logic to it. if you think if other is better lmk. perhaps this will clear by itself once we clear up our nfc routine i guess.


2A: NFC Workflow Definition (CRITICAL - MOST UNDEFINED)
Entry sequence: What button sequence enters NFC scan mode?
-- not sure, my current firmware just does simple button uplinks, nor do i register long press etc, that i dont have a UI/Statemachine. ideally it will have to be some secret routine that customers cannot accidently do it. we need to work on this and decide what makes the most sense once we have other reuqiremtns/ possbilites scoped out. 

Long-press? Simultaneous press? Sequential combo?
-- not sure. i was thinking long press to "enter some mode" and then from there some combo initates other things. we can decide this later.

Check-in vs Check-out differentiation: How does the system know which one?
Different sequences for each?
Toggle mode (1st scan = in, 2nd scan = out)?
-- yes. whatever is easy from firemware pov. 

State tracking per UUID?
-- i dont want to state track. i want to uplink all required data i can for the backend to add logic to it. we keep our logic agnostic and uplink data first. lmk if this is okay or we can tlak about more.

Registered Vote flow:
Is it: Enter NFC mode → Scan → Press button(s) → Exit?
-- yes that would be good. after exit, the payload is preped and uplinked

Can employee press multiple buttons for one scan (smell + cleanliness)?
-- hmm, i was thinking we can keep it one vote in one cycle and if they have multiple issues they can repeat the process as needed. 

How does employee "exit" registered vote mode?
-- maybe after uplink it goes back to our regular customer facing mode. if they want to do it again they can repeat the process?

Timeout: How long does device wait for NFC tap after entering scan mode?
-- 5 second maybe? 

Edge case: Custodian checks in but never checks out - what happens?
-- yes I was thinking about this too. im not sure. suggest me solutions. the repurcisons are uplinking, epd dispaly. 


2B: EPD Timing & States
"Thank you for feedback" → how many seconds before returning to "Last Cleaned"?
-- lets say 5 seconds. that'll match our cooldown for button spamming too. 

Does "Cleaning in progress" block customer button presses?
-- im thinking about this too. im not sure add these too questions i need to ask clients. 

If backend downlink updates EPD, does it override local RTC timestamp?
-- no it wont override local rtc. 

2C: LED Behavior
LED on-time after button press? (500ms? 1s? 2s?)
-- maybe 1 second blink

Different LED pattern when button is rejected (during lockout)?
-- no led just wont light up so that user know their vote is not happening 

LED feedback during NFC scan mode? (blinking to indicate "waiting for card"?)
-- yes we need to hone this. this LED is going to have comprehensive UI work. once we have the fucntioanclites locked down for eveyr state, we can revist LED and decice what the UI will be at eveyr state

D: LoRa Downlink & Heartbeat
Heartbeat interval: How often? (Impacts battery significantly)
Suggestion: Every 1-6 hours is reasonable for Class A battery device
-- well exactly depending upon what we hearbeating, but i think once a day is fine. 

Downlink latency expectation: Customer must understand EPD updates arrive only after next uplink. Is "within next heartbeat interval" acceptable?
-- correct. if thats the case can we send some pseudo uplink / mac command to open rx window? also lets say backend has queued things, and the customer presses button, then the rx will open. in that case i woudln't want them to see the display update. it will be bad ux as they will think their button press updated the system. lets flesh this out proeprly. how do we acutally handle downlink.

Proposed downlink commands (for your consideration):
0x01 - Update EPD "Last Cleaned" text (with timestamp payload)
0x02 - Full EPD refresh (clear ghosting)
0x03 - Request status report (device uplinks battery %, counters, etc.)
0x04 - Reset button counters
Any others you need?
-- not sure, i like these 4 for now. 

one more thing i am thinking is since these devices will be flashed with same firmware, how will take care of case where each device all do their heartbeat at the same time. in that case could we see collison? In that case does it make snese to offload heartbeat to backend? their lambda fucntion can shceudle their downlink queery at differnt time. lmk if this is correct way to htink?


2E: Error Handling
LoRa join failure on boot: Retry forever? Show on EPD? Timeout?
-- yes maybe we try 10 times and maybe sleep for sometime between each try. if that doesnt happen, it reboots. after 3 tries,, the system will shutdown. at that point, the customer will have to power cycle (remove and reinsert battery)

NFC read failure: Feedback to employee?
-- yes i think our LED should tell in either case. dont know exactly, we think about this when we flesh our LED ui per state.

SDHC missing/full: Does device still work?
-- yes device still works. 
-- not sure about full, maybe we can add this as an event type to tell when sd card is reching full status.




---

**Cursor**

# Phase 2 Progress - My Recommendations & Remaining Questions

Excellent progress! You've given me enough to make concrete recommendations on several open items. Let me address your questions, then we'll lock down the remaining ambiguities.

---

## My Recommendations on Open Items

### 1. NFC Event Types (Confirmed Approach)

Separate event types is the right call - keeps device logic stateless and pushes interpretation to backend:

```
Event Types:
0x00 = Button Press (public feedback)
0x01 = NFC Check-in
0x02 = NFC Check-out
0x03 = NFC Registered Vote (includes Button ID)
0x04 = Heartbeat / Status
0x05 = Low Battery Alert
0x06 = SD Card Near Full (future)
...
0xFF = Reserved
```

---

### 2. NFC Mode Entry - My Recommendation

Since you want a "secret" sequence customers can't accidentally trigger, here's my suggestion:

**Recommended: Simultaneous 2-Button Press (Button 0 + Button 5)**
- Nearly impossible to press accidentally with one hand
- Intuitive for trained employees ("press the two end buttons together")
- Easy to implement: detect both buttons pressed within 100ms window
- Hold for 2 seconds to confirm intent

**After entering NFC mode:**
- LED blinks to indicate "waiting for card"
- 5-second timeout for card tap
- If timeout: return to normal mode, no uplink

---

### 3. Check-in vs Check-out Without State Tracking

Since you want to keep the device stateless (just uplink data, let backend handle logic), **the employee must declare intent via the entry sequence**:

**Option A: Different Entry Sequences**
| Action | Sequence | Result |
|--------|----------|--------|
| Check-in | Button 0+5 (2s hold) → Tap card | Uplink: Event 0x01 + NDEF ID |
| Check-out | Button 1+4 (2s hold) → Tap card | Uplink: Event 0x02 + NDEF ID |
| Registered Vote | Button 2+3 (2s hold) → Tap card → Press button | Uplink: Event 0x03 + NDEF ID + Button ID |

**Option B: Single Entry, Context from Next Action**
| Sequence | Result |
|----------|--------|
| Button 0+5 (2s hold) → Tap card → Press Button 0 | Check-in (Button 0 = "IN") |
| Button 0+5 (2s hold) → Tap card → Press Button 5 | Check-out (Button 5 = "OUT") |
| Button 0+5 (2s hold) → Tap card → Press Button 1-4 | Registered Vote with that button |

**I recommend Option B** - single entry point is easier to remember, and you can label Button 0 as "IN" and Button 5 as "OUT" on the employee-facing decal/instructions. Buttons 1-4 become the "issue buttons" for registered votes.

**Flow:**
```
Normal Mode
    │
    ▼ (Button 0+5 held 2s)
┌─────────────────────────┐
│   NFC SCAN MODE         │
│   LED: Blinking         │
│   Timeout: 5s           │
└─────────────────────────┘
    │
    ▼ (Card tapped, NDEF read)
┌─────────────────────────┐
│   AWAITING BUTTON       │
│   LED: Solid ON         │
│   Timeout: 10s          │
└─────────────────────────┘
    │
    ├─► Button 0 pressed → Uplink Check-in (0x01) → Show "Cleaning in progress"
    ├─► Button 5 pressed → Uplink Check-out (0x02) → Show "Last Cleaned: [RTC time]"
    └─► Button 1-4 pressed → Uplink Registered Vote (0x03 + Button ID) → Return to normal
```

---

### 4. Custodian Forgets to Check Out

Since device is stateless, this is **purely a backend problem** - which is good! 

**Backend can handle it by:**
- Tracking open sessions per NDEF ID
- Auto-closing sessions after configurable timeout (e.g., 4 hours)
- Alerting operations if check-out not received
- The EPD will still show stale "Last Cleaned" time until next check-out

**Device behavior:** No change needed. It just sends uplinks. The "Cleaning in progress" text stays until next check-out (or power cycle, or downlink override).

**Question for customer:** Is "Cleaning in progress" stuck on EPD acceptable until next check-out? Or should there be a local timeout that reverts to "Last Cleaned"?

---

### 5. Downlink Handling - Solving the UX Problem

You raised a great point: if customer presses button and a queued downlink updates the EPD, it looks like their button caused it.

**My Recommendation: Downlink Processing Windows**

| Uplink Source | Process EPD-Update Downlinks? |
|---------------|-------------------------------|
| Button Press (0x00) | **NO** - Only show "Thank you", ignore EPD updates |
| NFC Check-out (0x02) | **NO** - EPD already updating from RTC |
| Heartbeat (0x04) | **YES** - Safe to process EPD updates |
| Low Battery (0x05) | **YES** - Safe to process |

**Implementation:**
- After button press uplink, if downlink contains EPD update command → queue it internally
- Process queued EPD commands on next heartbeat window OR after 1-hour delay (whichever first)
- Non-EPD commands (status request, counter reset) can process immediately

This keeps UX clean while still allowing backend to push EPD updates.

---

### 6. Heartbeat Collision - Solving the Thundering Herd

You're right that all devices with same firmware doing daily heartbeat at same time is bad.

**My Recommendation: DevEUI-Based Jitter**

```
heartbeat_offset = (DevEUI[7] * 256 + DevEUI[6]) % (24 * 60)  // Minutes offset
daily_heartbeat_time = 00:00 UTC + heartbeat_offset minutes
```

This distributes heartbeats across the full 24-hour window deterministically based on each device's unique DevEUI. No coordination needed, no collisions.

**Backend-controlled heartbeat timing** is problematic because:
- Requires downlink to set the time
- Class A means you can't guarantee when device receives it
- Creates chicken-and-egg problem

DevEUI jitter is self-organizing and collision-free.

---

### 7. "Pseudo Uplink" for RX Window

Yes, technically you can send an empty/minimal uplink just to open RX. But:
- Costs battery (LoRa TX is expensive)
- Uses airtime (regulatory limits)
- Defeats purpose of Class A

**Better approach:** Accept that EPD updates have latency bounded by heartbeat interval + next button press. If customer needs faster updates, you could offer a "request update" button combo that triggers an uplink specifically to receive queued downlinks. But I'd add this as a future feature, not v1.1.

---

## Updated Assumptions List

| # | Item | Status |
|---|------|--------|
| A1 | Button IDs 0-5 | ✅ Confirmed |
| A2 | 5s lockout for public only | ✅ Confirmed |
| A3 | NFC reads 4-byte NDEF user ID | ✅ Confirmed |
| A4 | Last Cleaned from RTC; backend can also push via downlink | ✅ Confirmed |
| A5 | Button counts persist across battery swap | ✅ Confirmed |
| A6 | Reed switch unused in v1.1 | ✅ Confirmed |
| A7 | Indoor only | ✅ Confirmed |
| A8 | Single LED for all feedback | ✅ Confirmed |
| A9 | NFC mode entry: Button 0+5 simultaneous hold (2s) | **PROPOSED** |
| A10 | Check-in/Check-out/Vote determined by button press after NFC scan (Option B) | **PROPOSED** |
| A11 | 5-second NFC scan timeout | ✅ Confirmed |
| A12 | Device is stateless; backend tracks sessions | ✅ Confirmed |
| A13 | "Thank you" display duration: 5 seconds | ✅ Confirmed |
| A14 | LED on-time: 1 second solid | ✅ Confirmed |
| A15 | No LED during lockout (silent rejection) | ✅ Confirmed |
| A16 | Heartbeat: Once daily with DevEUI-based jitter | **PROPOSED** |
| A17 | Downlink EPD updates only processed after heartbeat, not button press | **PROPOSED** |
| A18 | Join retry: 10 attempts with sleep between, 3 boot cycles then shutdown | ✅ Confirmed |
| A19 | Device operates without SDHC (logging lost, counters still work via EEPROM) | ✅ Confirmed |
| A20 | Does "Cleaning in progress" block customer buttons? | **ASK CUSTOMER** |
| A21 | If custodian never checks out, does EPD revert after timeout? | **ASK CUSTOMER** |

---

## Remaining Questions Before Document

### For You (Firmware Scope):

1. **Do you agree with my recommendations above?** (NFC entry sequence, Option B for check-in/out, downlink windows, DevEUI jitter)

2. **Downlink commands - confirm these 4 are sufficient for v1.1:**
   - `0x01` - Update EPD "Last Cleaned" text
   - `0x02` - Full EPD refresh
   - `0x03` - Request status uplink
   - `0x04` - Reset button counters

3. **RTC synchronization:** How does the RTC get set initially? 
   - At factory provisioning?
   - Via downlink command?
   - GPS/network time?

4. **Boot sequence:** What should happen when device powers on?
   - Join LoRa network
   - Display default EPD content ("Last Cleaned: --:--" if no previous timestamp?)
   - Send heartbeat/status uplink?
   - What if EEPROM has no previous state (fresh device)?

### For Customer (Add to Validation List):

1. Does "Cleaning in progress" block customer button presses?
2. If custodian forgets check-out, should EPD auto-revert to "Last Cleaned" after X hours?
3. Is once-daily heartbeat (with up to 24hr downlink latency) acceptable?
4. Confirm Button 0 = "IN", Button 5 = "OUT" mapping is acceptable for employee workflow

---

**Once you confirm or adjust my recommendations, I'll have enough to draft the full requirements document.**

```plaintext
Event Types:
0x00 = Button Press (public feedback)
0x01 = NFC Check-in
0x02 = NFC Check-out
0x03 = NFC Registered Vote (includes Button ID)
0x04 = Heartbeat / Status
0x05 = Low Battery Alert
0x06 = SD Card Near Full (future)
...
0xFF = Reserved
```

```plaintext
Normal Mode
    │
    ▼ (Button 0+5 held 2s)
┌─────────────────────────┐
│   NFC SCAN MODE         │
│   LED: Blinking         │
│   Timeout: 5s           │
└─────────────────────────┘
    │
    ▼ (Card tapped, NDEF read)
┌─────────────────────────┐
│   AWAITING BUTTON       │
│   LED: Solid ON         │
│   Timeout: 10s          │
└─────────────────────────┘
    │
    ├─► Button 0 pressed → Uplink Check-in (0x01) → Show "Cleaning in progress"
    ├─► Button 5 pressed → Uplink Check-out (0x02) → Show "Last Cleaned: [RTC time]"
    └─► Button 1-4 pressed → Uplink Registered Vote (0x03 + Button ID) → Return to normal
```

```plaintext
heartbeat_offset = (DevEUI[7] * 256 + DevEUI[6]) % (24 * 60)  // Minutes offset
daily_heartbeat_time = 00:00 UTC + heartbeat_offset minutes
```

---

**User**



4. Custodian Forgets to Check Out:
1. so we can interepet this in 2 ways, we can set a window that assumes that the custodian is stil lcleaning. after that acceptable window passes, we would think that they forgot to checkout. in that case, there are 2 things we can do, revert to our last state timestamp, or write new timestamp which is bascially current rtc at that moment (whcih means after check in + that timeout) . still not sure, so put this in assumption list i should send to cleint.


Downlink Handling - Solving the UX Problem
yes this sounds good.  

thundering herd solution also looks good

ok heard on psuedo uplink. whatwe have currently is good.

2. yes downlink commnads looks good.

3. rtc will sycnronize by send MAC command to LNS. so myabe after joining it wil lsend mac command, get time, update its rtc.

4. yes def joins lora, gets rtc, and then fills last cleaned with that time stamp it just got (just to fill it up and not leave it empty) is that ok. no worries if eeprom is fresh, it just waits for people to press buttons





some extra points:
1. There will be 2 variants of the firmware, one will have everthing but the epd. this shouldn't really change something big but just putting it out there.
1. i am thinking we press some button sequence to "query" the creds and status of device on the epd. for example we could see the eui that the device is flashed with, "Company Name" and firmware version.
2. im also thinking we should have some kind of housekeeping routine, for example we send some crucila mac commands (not sure what, eg rtc, to update itsself and remove drift etc), give me suggestions what else should our system do in this housekeeping if needed.
3. give me more suggestiosn/ startup flow our system should have i havent thought much about it.

hold onto developing doc for now lets hone every aspect


---

**Cursor**

# Continuing Requirements Refinement

Great, let's keep honing. You've raised important new aspects. Let me address each systematically.

---

## 1. Firmware Variants (EPD vs No-EPD)

This is significant and needs clear scoping.

**Variant A: Full (with EPD)**
- All features as discussed

**Variant B: No EPD**

| Feature | Variant A (EPD) | Variant B (No EPD) |
|---------|-----------------|---------------------|
| Button feedback | LED + "Thank you" on EPD | LED only |
| Last Cleaned display | Shown on EPD | Not applicable |
| Cleaning in progress | Shown on EPD | Not applicable (LED pattern?) |
| NFC check-in/out | Full flow + EPD update | Flow works, no display |
| Device info query | Shows on EPD | ??? |
| Downlink EPD update | Yes | Command ignored / N/A |

**Questions for Variant B:**
1. Why would a customer choose no-EPD variant? (Cost? Different mounting?)
2. For Variant B, should there be a unique LED pattern for check-in/check-out confirmation since there's no EPD?
3. Device info query on Variant B - skip feature entirely, or output via LoRa uplink?

**Recommendation:** Keep both variants from same codebase with compile-time flag. EPD-related code is conditionally included. This ensures feature parity where applicable and single source of truth.

---

## 2. Device Info Query Mode

**Proposed Entry Sequence:** Button 0+1+5 simultaneous hold (3 seconds)
- Different from NFC mode (0+5) to avoid confusion
- Three-button combo is very unlikely to happen accidentally

**Information to Display on EPD:**

```
┌────────────────────────────┐
│   DEVICE INFORMATION       │
├────────────────────────────┤
│ DevEUI: 0011223344556677   │
│ AppEUI: AABBCCDD11223344   │
│ FW Ver: 1.1.0              │
│ Model:  FB-NOW-A (EPD)     │
├────────────────────────────┤
│ Battery: 87%               │
│ Joined: Yes                │
│ RSSI: -72 dBm              │
│ RTC: 2026-01-22 14:35      │
├────────────────────────────┤
│ Counters:                  │
│ B0:1234 B1:892 B2:445      │
│ B3:221  B4:88  B5:12       │
│ Total: 2892                │
├────────────────────────────┤
│ [Any button to exit]       │
└────────────────────────────┘
```

**Behavior:**
- Timeout: 30 seconds → return to normal mode
- Any single button press → exit immediately
- During info mode: button presses don't count as votes, no uplinks

**For Variant B (No EPD):** 
- Option 1: Skip feature entirely
- Option 2: Trigger a "status report" uplink (Event Type 0x04) that backend can display in dashboard
- I recommend Option 2 - still useful for field technicians with backend access

---

## 3. Housekeeping Routine - Comprehensive Suggestions

Housekeeping should run **tied to the daily heartbeat** - natural periodic event, no extra wake-ups.

**Proposed Housekeeping Tasks:**

| Task | Purpose | Frequency |
|------|---------|-----------|
| **RTC Resync** | Correct drift (typical RTC drifts ~2 sec/day) | Daily (via DeviceTimeReq MAC) |
| **Battery ADC Sample** | Update battery % for heartbeat payload | Daily |
| **EEPROM → SDHC Flush** | Persist counters to SD card | Daily |
| **Link Check** | Verify network connectivity (LinkCheckReq MAC) | Daily or weekly |
| **EPD Full Refresh** | Clear ghosting from partial updates | Weekly (every 7th heartbeat) |
| **Memory Diagnostic** | Log heap usage, detect leaks (Zephyr) | Daily (log only, no uplink) |
| **Watchdog Feed** | If HW watchdog enabled, feed it | Continuous (not housekeeping) |

**Additional Suggestions:**

1. **Counter Checksum Validation**
   - EEPROM can get corrupted (bit flips, incomplete writes)
   - Store counters with CRC; on housekeeping, validate and repair if backup exists on SDHC

2. **SDHC Health Check**
   - Check remaining space
   - If < 10% remaining, set flag for "SD Near Full" uplink event

3. **Join Session Refresh** (Optional)
   - Some LoRaWAN networks expect periodic rejoin or session refresh
   - Could trigger rejoin if no successful uplink ACK in X days

4. **Log Rotation**
   - If SDHC logs daily, rotate/archive old logs
   - Keep last 30 days, compress or delete older

---

## 4. Startup/Boot Flow - Comprehensive Sequence

Here's a detailed boot sequence covering normal boot, first boot, and error cases:

```
┌─────────────────────────────────────────────────────────────────┐
│                      POWER ON / RESET                           │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 1. HARDWARE INIT                                                │
│    • GPIO (buttons, LED, chip selects)                          │
│    • SPI bus (EPD, LoRa, NFC, SDHC)                             │
│    • I2C bus (RTC, EEPROM)                                      │
│    • ADC (battery monitoring)                                   │
│    • LED: Single blink to indicate power-on                     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 2. EEPROM INIT                                                  │
│    • Read config block (DevEUI, AppEUI, AppKey)                 │
│    • Read counter block (button counts, devnonce)               │
│    • Validate CRC                                               │
│    ├─► CRC OK: Load values                                      │
│    └─► CRC FAIL: Use defaults, set "first boot" flag            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 3. SDHC INIT                                                    │
│    • Attempt mount                                              │
│    ├─► Success: Log "Boot" entry with RTC time                  │
│    └─► Fail: Set sdhc_available = false, continue               │
│    • (Device operates without SDHC, just no logging)            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 4. RTC INIT                                                     │
│    • Read current time                                          │
│    • Check validity (year > 2024?)                              │
│    ├─► Valid: Use as-is (may have drift)                        │
│    └─► Invalid: Set rtc_needs_sync = true                       │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 5. EPD INIT (Variant A only)                                    │
│    • Initialize display controller                              │
│    • Show boot screen:                                          │
│      ┌──────────────────────┐                                   │
│      │  [COMPANY LOGO]      │                                   │
│      │  Connecting...       │                                   │
│      │  FW: 1.1.0           │                                   │
│      └──────────────────────┘                                   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 6. LORA INIT                                                    │
│    • Initialize SX1262                                          │
│    • Load credentials from EEPROM                               │
│    • Load devnonce (for OTAA replay protection)                 │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 7. LORA JOIN (OTAA)                                             │
│    • Attempt join                                               │
│    • Retry logic: 10 attempts, exponential backoff              │
│      (e.g., 5s, 10s, 20s, 40s, 60s, 60s, 60s...)               │
│    • LED: Blink pattern during join attempts                    │
│    ├─► Success: Save session, increment devnonce in EEPROM      │
│    └─► Fail after 10: Reboot. After 3 boot cycles: SHUTDOWN     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 8. RTC SYNC (Post-Join)                                         │
│    • Send DeviceTimeReq MAC command                             │
│    • Wait for DeviceTimeAns in RX window                        │
│    ├─► Success: Update RTC, clear rtc_needs_sync                │
│    └─► Fail: Use existing RTC time (may be stale)               │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 9. EPD UPDATE (Variant A only)                                  │
│    • Display: "LAST CLEANED AT: [RTC timestamp]"                │
│    • (On first boot, this is "now" - acceptable as discussed)   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 10. NFC INIT                                                    │
│     • Initialize PN5180                                         │
│     • Put in low-power standby (activated on-demand)            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 11. SEND BOOT/HEARTBEAT UPLINK                                  │
│     • Event Type: 0x04 (Status)                                 │
│     • Payload: Battery %, boot reason, FW version               │
│     • This also opens RX window for any queued downlinks        │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 12. ENTER NORMAL OPERATION                                      │
│     • Enable button interrupts                                  │
│     • Start heartbeat timer (with DevEUI jitter offset)         │
│     • Enter low-power wait state                                │
│     • LED: Off (ready state)                                    │
└─────────────────────────────────────────────────────────────────┘
```

---

## 5. Additional Startup/Operational Considerations

**Questions I haven't seen addressed:**

### A. Credential Provisioning
How do DevEUI, AppEUI, and AppKey get into EEPROM?
- Factory programming via SWD/JTAG?
- Initial USB/serial provisioning?
- Derived from chip unique ID?

### B. Factory Reset Mechanism
Should there be a way to reset device to defaults?
- Clear counters?
- Clear LoRa session (force rejoin)?
- Wipe SDHC logs?
- Button combo (e.g., hold all 6 buttons for 10 seconds)?

### C. Deep Sleep vs Light Sleep
- **Between button presses:** Deep sleep (lowest power)
- **During NFC scan timeout:** Light sleep (need quick response)
- **During LoRa TX/RX:** Active
- What wakes from deep sleep? (Button GPIO interrupt, RTC alarm for heartbeat)

### D. Watchdog Strategy
- Hardware watchdog to recover from firmware hangs?
- Timeout value? (e.g., 30 seconds)
- What should trigger a watchdog reset vs. graceful recovery?

### E. Power Failure During Write
- What if battery dies during EEPROM write?
- Wear leveling on EEPROM? (Limited write cycles ~1M)
- Write-ahead logging on SDHC?

### F. LED UI Summary (Need to Define)
| State | LED Behavior |
|-------|--------------|
| Boot/power on | Single 500ms blink |
| Joining LoRa | Slow blink (1Hz) |
| Join failed | Fast blink 3x then off |
| Normal/idle | Off |
| Button press accepted | Solid 1 second |
| Button press rejected (lockout) | Off (no feedback) |
| NFC mode waiting for card | Fast blink (2Hz) |
| NFC card read success | Solid 2 seconds |
| NFC card read failed | Off (no feedback? or 3 fast blinks?) |
| Low battery | ??? |
| Device info mode | Solid on while displayed |

---

## Updated Assumptions List

| # | Item | Status |
|---|------|--------|
| A1-A8 | (Previous items) | ✅ Confirmed |
| A9 | NFC mode entry: Button 0+5 hold 2s | ✅ Confirmed |
| A10 | Check-in/out via button after NFC (Option B) | ✅ Confirmed |
| A11-A19 | (Previous items) | ✅ Confirmed |
| A20 | Does "Cleaning in progress" block customer buttons? | **ASK CUSTOMER** |
| A21 | Custodian no-checkout timeout behavior? | **ASK CUSTOMER** |
| A22 | Two firmware variants: with EPD, without EPD | ✅ Confirmed |
| A23 | Device info query: Button 0+1+5 hold 3s | **PROPOSED** |
| A24 | Housekeeping runs on daily heartbeat | **PROPOSED** |
| A25 | Boot shows "Connecting..." then "Last Cleaned: [RTC time]" | **PROPOSED** |
| A26 | RTC sync via DeviceTimeReq MAC after join | ✅ Confirmed |
| A27 | Initial "Last Cleaned" = RTC time at first boot | ✅ Confirmed |
| A28 | How are LoRa credentials provisioned? | **CLARIFY** |
| A29 | Is factory reset mechanism needed? | **CLARIFY** |
| A30 | Deep sleep wake sources: Button GPIO + RTC alarm | **PROPOSED** |
| A31 | Hardware watchdog enabled? Timeout? | **CLARIFY** |
| A32 | Variant B (no EPD) device info query via uplink | **PROPOSED** |

---

## Questions for You

1. **Firmware variants** - What's the use case for no-EPD variant? This helps me understand what feedback mechanisms matter.

2. **Credential provisioning** - How do you plan to program DevEUI/AppEUI/AppKey into each device?

3. **Factory reset** - Should technicians be able to reset a device in the field? What should reset?

4. **Watchdog** - Do you want hardware watchdog protection? (Recommended for unattended devices)

5. **Do the proposed sequences work?**
   - Device info: Button 0+1+5 hold 3s
   - NFC mode: Button 0+5 hold 2s
   - Factory reset (if needed): All buttons 10s?

6. **LED behavior** - Does my proposed LED UI table look reasonable? Any additions for NFC read fail?

---

**Let me know your answers and any adjustments. We're getting close to having all functional requirements locked down.**

```plaintext
┌────────────────────────────┐
│   DEVICE INFORMATION       │
├────────────────────────────┤
│ DevEUI: 0011223344556677   │
│ AppEUI: AABBCCDD11223344   │
│ FW Ver: 1.1.0              │
│ Model:  FB-NOW-A (EPD)     │
├────────────────────────────┤
│ Battery: 87%               │
│ Joined: Yes                │
│ RSSI: -72 dBm              │
│ RTC: 2026-01-22 14:35      │
├────────────────────────────┤
│ Counters:                  │
│ B0:1234 B1:892 B2:445      │
│ B3:221  B4:88  B5:12       │
│ Total: 2892                │
├────────────────────────────┤
│ [Any button to exit]       │
└────────────────────────────┘
```

```plaintext
┌─────────────────────────────────────────────────────────────────┐
│                      POWER ON / RESET                           │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 1. HARDWARE INIT                                                │
│    • GPIO (buttons, LED, chip selects)                          │
│    • SPI bus (EPD, LoRa, NFC, SDHC)                             │
│    • I2C bus (RTC, EEPROM)                                      │
│    • ADC (battery monitoring)                                   │
│    • LED: Single blink to indicate power-on                     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 2. EEPROM INIT                                                  │
│    • Read config block (DevEUI, AppEUI, AppKey)                 │
│    • Read counter block (button counts, devnonce)               │
│    • Validate CRC                                               │
│    ├─► CRC OK: Load values                                      │
│    └─► CRC FAIL: Use defaults, set "first boot" flag            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 3. SDHC INIT                                                    │
│    • Attempt mount                                              │
│    ├─► Success: Log "Boot" entry with RTC time                  │
│    └─► Fail: Set sdhc_available = false, continue               │
│    • (Device operates without SDHC, just no logging)            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 4. RTC INIT                                                     │
│    • Read current time                                          │
│    • Check validity (year > 2024?)                              │
│    ├─► Valid: Use as-is (may have drift)                        │
│    └─► Invalid: Set rtc_needs_sync = true                       │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 5. EPD INIT (Variant A only)                                    │
│    • Initialize display controller                              │
│    • Show boot screen:                                          │
│      ┌──────────────────────┐                                   │
│      │  [COMPANY LOGO]      │                                   │
│      │  Connecting...       │                                   │
│      │  FW: 1.1.0           │                                   │
│      └──────────────────────┘                                   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 6. LORA INIT                                                    │
│    • Initialize SX1262                                          │
│    • Load credentials from EEPROM                               │
│    • Load devnonce (for OTAA replay protection)                 │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 7. LORA JOIN (OTAA)                                             │
│    • Attempt join                                               │
│    • Retry logic: 10 attempts, exponential backoff              │
│      (e.g., 5s, 10s, 20s, 40s, 60s, 60s, 60s...)               │
│    • LED: Blink pattern during join attempts                    │
│    ├─► Success: Save session, increment devnonce in EEPROM      │
│    └─► Fail after 10: Reboot. After 3 boot cycles: SHUTDOWN     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 8. RTC SYNC (Post-Join)                                         │
│    • Send DeviceTimeReq MAC command                             │
│    • Wait for DeviceTimeAns in RX window                        │
│    ├─► Success: Update RTC, clear rtc_needs_sync                │
│    └─► Fail: Use existing RTC time (may be stale)               │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 9. EPD UPDATE (Variant A only)                                  │
│    • Display: "LAST CLEANED AT: [RTC timestamp]"                │
│    • (On first boot, this is "now" - acceptable as discussed)   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 10. NFC INIT                                                    │
│     • Initialize PN5180                                         │
│     • Put in low-power standby (activated on-demand)            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 11. SEND BOOT/HEARTBEAT UPLINK                                  │
│     • Event Type: 0x04 (Status)                                 │
│     • Payload: Battery %, boot reason, FW version               │
│     • This also opens RX window for any queued downlinks        │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 12. ENTER NORMAL OPERATION                                      │
│     • Enable button interrupts                                  │
│     • Start heartbeat timer (with DevEUI jitter offset)         │
│     • Enter low-power wait state                                │
│     • LED: Off (ready state)                                    │
└─────────────────────────────────────────────────────────────────┘
```

