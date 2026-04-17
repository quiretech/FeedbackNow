# FlexBox v1.2 — Gateway Test Plan

Use this flow with gateways **on** or **off** and with **downlink control** to uncover issues and edge cases. Run each section, capture **serial logs** and **your observations**, then we can fix issues together.

**Prerequisites:** Serial console (e.g. 115200 8N1), device powered from battery or USB. Optional: set `CONFIG_LOG_DEFAULT_LEVEL_DBG` in `prj.conf` for more detail (then set back to 3 for INFO for normal test).

---

## 1. Join / No-Join (gateway off vs on)

### 1.1 First boot, no gateway (gateway OFF)

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 1.1.1 | Power off gateway(s). Ensure device cannot hear any gateway. | — | — |
| 1.1.2 | **Factory reset** device (so `has_joined_once` is false): send **downlink 0x06** (factory reset). Wait for device to reboot. | Log line like `[SMF] factory reset: has_joined_once cleared` and reboot. | Device reboots; next boot is “first boot”. |
| 1.1.3 | Power cycle device (or leave it running after 1.1.2). | Boot log. | Log shows `FIRST BOOT: waiting for join command (Staff + 0+1+2)`. No join attempts. |
| 1.1.4 | Press **single button** (e.g. button 0). | Log + LED + EPD. | LED blinks, Thanks on EPD, log `Not joined; dropped button id=0`. Counter still increments (check later when joined). |
| 1.1.5 | Enter **Staff** (0+1 hold 2s), then **deliberate join** (0+1+2 hold 3s). | Log. | Join attempts start; all fail (gateway off). Log `Join timed out - will retry in 10 seconds`, retries. |

**Notes / issues you see:**  
*(e.g. wrong message, no LED, join runs when it shouldn’t)*
1. public buttons accpeted, but i could spam it and it flooded the epd display screen work (it should behave identically when joined, that is cooldown)
2. it did store the couteres ( i checked by seeing device info screen)
3. LOGS OF THIS RUN:
*** Booting nRF Connect SDK v3.0.2-89ba1294ac9b ***
*** Using Zephyr OS v4.0.99-f791c49f492c ***
[00:00:02.270,477] <inf> main: 
[00:00:02.270,507] <inf> main: === LoRaWAN Application Starting ===
[00:00:03.270,690] <inf> power_ctrl: Initialized power GPIO 0 on gpio@50000000 pin 14
[00:00:03.270,751] <inf> power_ctrl: Initialized power GPIO 1 on gpio@50000000 pin 15
[00:00:03.270,843] <inf> power_ctrl: Initialized power GPIO 2 on gpio@50000000 pin 16
[00:00:03.270,904] <inf> power_ctrl: Initialized power GPIO 3 on gpio@50000300 pin 2
[00:00:04.271,026] <inf> main: I2C bus ready
[00:00:04.321,197] <inf> payload_gen: payload_gen initialized
[00:00:04.321,258] <inf> leds: Initialized LED 0 on gpio@50000000 pin 13
[00:00:04.321,289] <inf> led_manager: LED manager initialized
[00:00:04.323,364] <inf> eeprom_probe: EEPROM probe OK
[00:00:04.323,394] <inf> rail_manager: Rail manager init (ref-counts 0)
[00:00:04.331,939] <inf> button_counter_store: Counter store loaded: slot=0 seq=0
[00:00:04.331,939] <inf> button_counter_store: Button counters (boot): b0=0 b1=0 b2=0 b3=0 b4=0 b5=0
[00:00:04.339,019] <inf> devnonce_store: DevNonce store loaded (slot=1 seq=475 last=12809)
[00:00:04.340,393] <inf> join_state_store: Join state: has_joined_once = 0
[00:00:04.341,857] <inf> rtc_app: RTC already initialized (2026-02-15 05:20:44)
[00:00:04.345,336] <inf> battery_adc: ADC internal reference voltage: 600 mV
[00:00:04.345,367] <inf> battery_adc: ADC initialized and calibrated
[00:00:04.405,761] <inf> pn5180: PN5180 initialized successfully
[00:00:04.411,010] <inf> nfc_svc: nfc_service initialized (start nfc_worker_id from main)
[00:00:04.411,071] <inf> display_mgr: [EPD] Setting buffers: size=15008 stride=50 mode=DIRECT
[00:00:04.452,575] <inf> display_mgr: display_manager init (EPD enabled, LVGL initialized)
[00:00:04.452,606] <inf> lora_app: SX1262: device ready.
[00:00:04.492,034] <inf> lora_app: Adaptive Data Rate (ADR) enabled
[00:00:04.492,065] <inf> lora_app: LoRaWAN stack initialized successfully.
[00:00:04.492,095] <inf> main: LED thread started
[00:00:04.492,126] <inf> main: NFC worker started
[00:00:04.492,156] <inf> main: LoRa thread started
[00:00:04.492,218] <inf> main: SMF thread started
[00:00:04.492,279] <inf> buttons: Button 0 on gpio@50000000 pin 11
[00:00:04.492,370] <inf> buttons: Button 1 on gpio@50000000 pin 12
[00:00:04.492,431] <inf> buttons: Button 2 on gpio@50000000 pin 24
[00:00:04.492,523] <inf> buttons: Button 3 on gpio@50000000 pin 25
[00:00:04.492,614] <inf> buttons: Button 4 on gpio@50000300 pin 1
[00:00:04.492,675] <inf> buttons: Button 5 on gpio@50000000 pin 28
[00:00:04.492,706] <inf> main: Input thread started
[00:00:04.492,736] <inf> main: Housekeeping thread started
[00:00:04.492,797] <inf> main: Rails released (idle); 3.3V/3.3A/3.6V off until requested
[00:00:04.492,858] <inf> smf: [SMF] thread started, state=Normal
[00:00:04.492,889] <inf> smf: [SMF] system ready (all go)
[00:00:04.492,950] <inf> lora_thread: 
[00:00:04.492,980] <inf> lora_thread: === LORA THREAD ENTRY ===
[00:00:04.493,011] <inf> lora_thread: LoRa thread started - Thread ID: 0x200007c8
[00:00:04.493,011] <inf> lora_thread: LoRa thread priority: 7
[00:00:04.493,041] <inf> lora_thread: LoRa thread stack size: 2048
[00:00:04.493,072] <inf> lora_thread: has_joined_once=0 (EEPROM_JOIN_STATE_CLEAR_ON_BOOT=0)
[00:00:04.493,072] <inf> lora_thread: 
[00:00:04.493,103] <inf> lora_thread: === FIRST BOOT: waiting for join command (Staff + 0+1+2) ===
[00:00:04.493,164] <inf> input: Input thread started (single + combo -> SMF)
[00:00:04.493,225] <inf> led_manager: LED UI thread started
[00:00:04.493,286] <inf> housekeeping: [housekeeping] thread started, jitter=1 offset_min=841
[00:00:07.577,148] <inf> ssd1683: Display initialization completed
[00:00:12.615,325] <inf> display_mgr: [EPD] show LOGO (FeedBackNow FlexBox)
[00:00:12.774,200] <inf> ssd1683: Display initialization completed
[00:00:30.481,048] <inf> input: [Input] btn=5 PRESS, held_before=0x20
[00:00:30.481,079] <inf> input: [Input] session_buttons=0x20 after press
[00:00:30.639,221] <inf> input: [Input] btn=5 RELEASE, held_before=0x0
[00:00:30.639,282] <inf> app_logic: [app_logic] public_vote called: button_id=5, now=30639
[00:00:30.639,587] <err> pcf8523: failed to read reg addr 0x00, len 10 (err -5)
[00:00:30.646,179] <wrn> lora_app: lora_put_event: Cannot queue message - not joined to network
[00:00:30.646,209] <wrn> app_logic: Not joined; dropped button id=5
[00:00:33.721,069] <inf> display_mgr: [EPD] show THANKS (5s then last cleaned)
[00:00:33.878,723] <inf> ssd1683: Display initialization completed
[00:00:38.823,913] <inf> button_counter_store: EEPROM counter flush OK (slot=1 seq=1)
[00:00:41.822,998] <inf> display_mgr: [EPD] show LAST_CLEANED 2026/02/14 21:11
[00:00:41.982,391] <inf> ssd1683: Display initialization completed
[00:00:45.200,561] <inf> input: [Input] btn=5 PRESS, held_before=0x0
[00:00:45.200,592] <inf> input: [Input] session_buttons=0x20 after press
[00:00:45.200,622] <inf> input: [Input] btn=5 RELEASE, held_before=0x20
[00:00:45.200,683] <inf> app_logic: [app_logic] public_vote called: button_id=5, now=45200
[00:00:45.202,178] <wrn> lora_app: lora_put_event: Cannot queue message - not joined to network
[00:00:45.202,209] <wrn> app_logic: Not joined; dropped button id=5
[00:00:48.300,079] <inf> display_mgr: [EPD] show THANKS (5s then last cleaned)
[00:00:48.458,648] <inf> ssd1683: Display initialization completed
[00:00:53.402,709] <inf> button_counter_store: EEPROM counter flush OK (slot=0 seq=2)
[00:00:56.400,726] <inf> display_mgr: [EPD] show LAST_CLEANED 2026/02/14 21:11
[00:00:56.557,952] <inf> ssd1683: Display initialization completed
[00:00:59.776,428] <inf> input: [Input] btn=5 PRESS, held_before=0x0
[00:00:59.776,428] <inf> input: [Input] session_buttons=0x20 after press
[00:00:59.776,489] <inf> input: [Input] btn=5 RELEASE, held_before=0x20
[00:00:59.776,550] <inf> app_logic: [app_logic] public_vote called: button_id=5, now=59776
[00:00:59.778,045] <wrn> lora_app: lora_put_event: Cannot queue message - not joined to network
[00:00:59.778,076] <wrn> app_logic: Not joined; dropped button id=5
[00:01:00.806,213] <inf> input: [Input] btn=5 PRESS, held_before=0x20
[00:01:00.806,243] <inf> input: [Input] session_buttons=0x20 after press
[00:01:01.000,915] <inf> input: [Input] btn=5 RELEASE, held_before=0x0
[00:01:01.000,976] <inf> app_logic: [app_logic] public_vote called: button_id=5, now=61000
[00:01:01.002,502] <wrn> lora_app: lora_put_event: Cannot queue message - not joined to network
[00:01:01.002,502] <wrn> app_logic: Not joined; dropped button id=5
[00:01:02.221,069] <inf> input: [Input] btn=5 PRESS, held_before=0x20
[00:01:02.221,099] <inf> input: [Input] session_buttons=0x20 after press
[00:01:02.404,296] <inf> input: [Input] btn=5 RELEASE, held_before=0x0
[00:01:02.404,388] <inf> app_logic: [app_logic] public_vote called: button_id=5, now=62404
[00:01:02.405,883] <wrn> lora_app: lora_put_event: Cannot queue message - not joined to network
[00:01:02.405,914] <wrn> app_logic: Not joined; dropped button id=5
[00:01:02.875,946] <inf> display_mgr: [EPD] show THANKS (5s then last cleaned)
[00:01:03.032,379] <inf> ssd1683: Display initialization completed
[00:01:06.250,610] <inf> input: [Input] btn=5 PRESS, held_before=0x0
[00:01:06.250,640] <inf> input: [Input] session_buttons=0x20 after press
[00:01:06.250,671] <inf> input: [Input] btn=5 RELEASE, held_before=0x20
[00:01:06.250,762] <inf> app_logic: [app_logic] public_vote called: button_id=5, now=66250
[00:01:06.250,885] <inf> input: [Input] btn=5 PRESS, held_before=0x0
[00:01:06.250,915] <inf> input: [Input] session_buttons=0x20 after press
[00:01:06.250,946] <inf> input: [Input] btn=5 RELEASE, held_before=0x20
[00:01:06.252,258] <wrn> lora_app: lora_put_event: Cannot queue message - not joined to network
[00:01:06.252,258] <wrn> app_logic: Not joined; dropped button id=5
[00:01:06.252,319] <inf> app_logic: [app_logic] public_vote called: button_id=5, now=66252
[00:01:06.253,875] <wrn> lora_app: lora_put_event: Cannot queue message - not joined to network
[00:01:06.253,906] <wrn> app_logic: Not joined; dropped button id=5
[00:01:06.351,135] <inf> input: [Input] btn=5 PRESS, held_before=0x20
[00:01:06.351,135] <inf> input: [Input] session_buttons=0x20 after press
[00:01:06.509,765] <inf> input: [Input] btn=5 RELEASE, held_before=0x0
[00:01:06.509,918] <inf> app_logic: [app_logic] public_vote called: button_id=5, now=66509
[00:01:06.511,444] <wrn> lora_app: lora_put_event: Cannot queue message - not joined to network
[00:01:06.511,444] <wrn> app_logic: Not joined; dropped button id=5
[00:01:07.976,104] <inf> button_counter_store: EEPROM counter flush OK (slot=1 seq=3)
[00:01:09.350,128] <inf> display_mgr: [EPD] show THANKS (5s then last cleaned)
[00:01:09.507,751] <inf> ssd1683: Display initialization completed
[00:01:17.449,584] <inf> display_mgr: [EPD] show THANKS (5s then last cleaned)
[00:01:17.605,102] <inf> ssd1683: Display initialization completed
[00:01:25.549,041] <inf> display_mgr: [EPD] show THANKS (5s then last cleaned)
[00:01:25.703,308] <inf> ssd1683: Display initialization completed
[00:01:33.648,498] <inf> display_mgr: [EPD] show THANKS (5s then last cleaned)
[00:01:33.804,260] <inf> ssd1683: Display initialization completed
[00:01:41.747,955] <inf> display_mgr: [EPD] show THANKS (5s then last cleaned)
[00:01:41.903,686] <inf> ssd1683: Display initialization completed
[00:01:49.848,541] <inf> display_mgr: [EPD] show LAST_CLEANED 2026/02/14 21:11
[00:01:50.005,859] <inf> ssd1683: Display initialization completed
[00:01:58.046,752] <inf> display_mgr: [EPD] show LAST_CLEANED 2026/02/14 21:11
[00:01:58.205,047] <inf> ssd1683: Display initialization completed
[00:02:06.244,079] <inf> display_mgr: [EPD] show LAST_CLEANED 2026/02/14 21:11
[00:02:06.401,184] <inf> ssd1683: Display initialization completed
[00:02:13.028,320] <inf> input: [Input] Start timing combo: ev=7, held=0x3, last_fired=0x0
[00:02:13.028,808] <inf> input: [Input] btn=1 PRESS, held_before=0x3
[00:02:13.028,808] <inf> input: [Input] session_buttons=0x2 after press
[00:02:13.039,825] <inf> input: [Input] btn=0 PRESS, held_before=0x3
[00:02:13.039,855] <inf> input: [Input] session_buttons=0x3 after press
[00:02:14.441,101] <inf> display_mgr: [EPD] show LAST_CLEANED 2026/02/14 21:11
[00:02:14.597,534] <inf> ssd1683: Display initialization completed
[00:02:17.816,986] <inf> input: [Input] btn=0 RELEASE, held_before=0x0
[00:02:17.817,016] <inf> input: [Input] btn=1 RELEASE, held_before=0x0
[00:02:22.638,122] <inf> display_mgr: [EPD] show LAST_CLEANED 2026/02/14 21:11
[00:02:22.796,417] <inf> ssd1683: Display initialization completed
[00:02:26.016,143] <inf> input: [Input] Start timing combo: ev=7, held=0x3, last_fired=0x0
[00:02:26.016,174] <inf> input: [Input] btn=0 PRESS, held_before=0x3
[00:02:26.016,204] <inf> input: [Input] session_buttons=0x1 after press
[00:02:26.016,235] <inf> input: [Input] btn=1 PRESS, held_before=0x3
[00:02:26.016,265] <inf> input: [Input] session_buttons=0x3 after press
[00:02:28.021,789] <inf> input: [Input] COMBO FIRED: ev=7, mask=0x3
[00:02:28.021,850] <inf> smf: [SMF] Normal -> Staff (LED solid, 20s timeout)
[00:02:28.021,942] <inf> input: [Input] After combo: session_combo_fired=true, last_fired=0x3
[00:02:29.241,790] <inf> input: [Input] btn=0 RELEASE, held_before=0x0
[00:02:29.263,854] <inf> input: [Input] btn=1 RELEASE, held_before=0x0
[00:02:30.837,402] <inf> display_mgr: [EPD] show LAST_CLEANED 2026/02/14 21:11
[00:02:30.994,873] <inf> ssd1683: Display initialization completed
[00:02:34.215,332] <inf> input: [Input] Start timing combo: ev=8, held=0x23, last_fired=0x0
[00:02:34.215,362] <inf> input: [Input] btn=0 PRESS, held_before=0x23
[00:02:34.215,393] <inf> input: [Input] session_buttons=0x1 after press
[00:02:34.215,454] <inf> input: [Input] btn=1 PRESS, held_before=0x23
[00:02:34.215,454] <inf> input: [Input] session_buttons=0x3 after press
[00:02:34.215,515] <inf> input: [Input] btn=5 PRESS, held_before=0x23
[00:02:34.215,515] <inf> input: [Input] session_buttons=0x23 after press
[00:02:37.223,175] <inf> input: [Input] COMBO FIRED: ev=8, mask=0x23
[00:02:37.223,297] <inf> smf: [SMF] Staff -> DeviceInfo (30s timeout)
[00:02:37.223,327] <inf> input: [Input] After combo: session_combo_fired=true, last_fired=0x23
[00:02:38.200,561] <inf> input: [Input] btn=5 RELEASE, held_before=0x2
[00:02:38.201,812] <inf> input: [Input] btn=0 RELEASE, held_before=0x2
[00:02:38.249,053] <inf> input: [Input] btn=1 RELEASE, held_before=0x2
[00:02:40.322,631] <inf> display_mgr: [EPD] show DEVICE_INFO
[00:02:40.486,816] <inf> ssd1683: Display initialization completed
[00:02:47.223,388] <inf> smf: [SMF] DeviceInfo -> Normal (timeout)
[00:02:50.323,913] <inf> display_mgr: [EPD] show LAST_CLEANED 2026/02/14 21:11
[00:02:50.480,804] <inf> ssd1683: Display initialization completed
[00:03:03.723,358] <inf> input: [Input] Start timing combo: ev=7, held=0x3, last_fired=0x0
[00:03:03.741,760] <inf> input: [Input] btn=0 PRESS, held_before=0x3
[00:03:03.741,760] <inf> input: [Input] session_buttons=0x1 after press
[00:03:03.757,934] <inf> input: [Input] btn=1 PRESS, held_before=0x3
[00:03:03.757,965] <inf> input: [Input] session_buttons=0x3 after press
[00:03:05.762,359] <inf> input: [Input] COMBO FIRED: ev=7, mask=0x3
[00:03:05.762,420] <inf> smf: [SMF] Normal -> Staff (LED solid, 20s timeout)
[00:03:05.762,512] <inf> input: [Input] After combo: session_combo_fired=true, last_fired=0x3
[00:03:06.529,327] <inf> input: [Input] btn=0 RELEASE, held_before=0x0
[00:03:06.556,945] <inf> input: [Input] btn=1 RELEASE, held_before=0x0
[00:03:09.012,359] <inf> input: [Input] Start timing combo: ev=9, held=0x7, last_fired=0x0
[00:03:09.024,749] <inf> input: [Input] btn=2 PRESS, held_before=0x7
[00:03:09.024,749] <inf> input: [Input] session_buttons=0x4 after press
[00:03:09.025,909] <inf> input: [Input] btn=0 PRESS, held_before=0x7
[00:03:09.025,909] <inf> input: [Input] session_buttons=0x5 after press
[00:03:09.034,179] <inf> input: [Input] btn=1 PRESS, held_before=0x7
[00:03:09.034,210] <inf> input: [Input] session_buttons=0x7 after press
[00:03:12.040,802] <inf> input: [Input] COMBO FIRED: ev=9, mask=0x7
[00:03:12.040,893] <inf> smf: [SMF] Staff -> Normal (deliberate join; trigger LoRa join)
[00:03:12.040,954] <inf> lora_thread: Join command received, starting join loop...
[00:03:12.041,046] <inf> lora_thread: 
[00:03:12.041,076] <inf> lora_thread: === STARTING LORA JOIN LOOP ===
[00:03:12.041,229] <inf> input: [Input] After combo: session_combo_fired=true, last_fired=0x7
[00:03:12.045,166] <inf> lora_thread: 
[00:03:12.045,166] <inf> lora_thread: === JOIN ATTEMPT 0 ===
[00:03:12.045,196] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:03:12.045,227] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:03:12.045,288] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:03:12.045,288] <inf> lora_thread: Joining network using OTAA, devNonce: 12810; attempt: 0
[00:03:13.227,142] <inf> input: [Input] btn=0 RELEASE, held_before=0x2
[00:03:13.227,355] <inf> input: [Input] btn=2 RELEASE, held_before=0x2
[00:03:13.271,697] <inf> input: [Input] btn=1 RELEASE, held_before=0x2
[00:03:15.140,289] <inf> display_mgr: [EPD] show CONNECTING
[00:03:15.290,557] <inf> ssd1683: Display initialization completed
[00:03:20.286,285] <err> lorawan: MlmeConfirm failed : Rx 1 timeout
[00:03:20.289,093] <inf> lora_thread: lorawan_join() returned: -116
[00:03:20.289,093] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:03:20.289,123] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:03:23.329,681] <inf> display_mgr: [EPD] show CONNECTING
[00:03:23.477,386] <inf> ssd1683: Display initialization completed
[00:03:30.293,334] <inf> lora_thread: 
[00:03:30.293,365] <inf> lora_thread: === JOIN ATTEMPT 1 ===
[00:03:30.293,395] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:03:30.293,426] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:03:30.293,457] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:03:30.293,457] <inf> lora_thread: Joining network using OTAA, devNonce: 12811; attempt: 1
[00:03:36.755,401] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:03:36.758,209] <inf> lora_thread: lorawan_join() returned: -116
[00:03:36.758,209] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:03:36.758,239] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:03:46.762,420] <inf> lora_thread: 
[00:03:46.762,451] <inf> lora_thread: === JOIN ATTEMPT 2 ===
[00:03:46.762,451] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:03:46.762,481] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:03:46.762,512] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:03:46.762,542] <inf> lora_thread: Joining network using OTAA, devNonce: 12812; attempt: 2
[00:03:53.224,273] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:03:53.227,081] <inf> lora_thread: lorawan_join() returned: -116
[00:03:53.227,081] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:03:53.227,111] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:04:03.231,292] <inf> lora_thread: 
[00:04:03.231,323] <inf> lora_thread: === JOIN ATTEMPT 3 ===
[00:04:03.231,323] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:04:03.231,384] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:04:03.231,414] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:04:03.231,414] <inf> lora_thread: Joining network using OTAA, devNonce: 12813; attempt: 3
[00:04:09.694,244] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:04:09.697,021] <inf> lora_thread: lorawan_join() returned: -116
[00:04:09.697,052] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:04:09.697,082] <inf> lora_thread: Sleeping for 10 seconds before retry...
---

### 1.2 First boot, gateway ON — deliberate join

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 1.2.1 | Keep device in “first boot” state (or factory reset 0x06 again, then power cycle). Gateway **ON**. | — | — |
| 1.2.2 | Enter **Staff** (0+1 hold 2s). | Log + LED. | `Normal -> Staff`, LED solid. |
| 1.2.3 | **Deliberate join**: hold 0+1+2 for 3s. | Log. | Join loop runs; `Joined network! DevAddr: ...`. Then counter-sync (Event 0x07) uplinks, time sync. |
| 1.2.4 | Press **single button** (e.g. button 0). | Log + uplink. | Button uplink on port 10, `Data sent on port 10`. |
| 1.2.5 | Power cycle device (gateway still ON). | Boot log. | `has_joined_once=1`; join starts automatically (no Staff combo). No “FIRST BOOT: waiting…”. |

**Notes / issues you see:**
yes it joined on attempt 10, all things worrked great
1. logs
[00:00:00.318,511] <inf> pn5180: PN5180 initialized successfully
[00:00:00.318,542] <inf> ssd1683_display: Initializing SSD1683 display driver
[00:00:00.401,824] <inf> ssd1683: Display initialization completed
[00:00:02.233,428] <inf> ssd1683: SSD1683 driver initialized
[00:00:02.233,459] <inf> ssd1683_display: SSD1683 display driver initialized successfully
*** Booting nRF Connect SDK v3.0.2-89ba1294ac9b ***
*** Using Zephyr OS v4.0.99-f791c49f492c ***
[00:00:02.270,294] <inf> main: 
[00:00:02.270,324] <inf> main: === LoRaWAN Application Starting ===
[00:00:03.270,507] <inf> power_ctrl: Initialized power GPIO 0 on gpio@50000000 pin 14
[00:00:03.270,568] <inf> power_ctrl: Initialized power GPIO 1 on gpio@50000000 pin 15
[00:00:03.270,660] <inf> power_ctrl: Initialized power GPIO 2 on gpio@50000000 pin 16
[00:00:03.270,721] <inf> power_ctrl: Initialized power GPIO 3 on gpio@50000300 pin 2
[00:00:04.270,843] <inf> main: I2C bus ready
[00:00:04.321,014] <inf> payload_gen: payload_gen initialized
[00:00:04.321,075] <inf> leds: Initialized LED 0 on gpio@50000000 pin 13
[00:00:04.321,105] <inf> led_manager: LED manager initialized
[00:00:04.323,181] <inf> eeprom_probe: EEPROM probe OK
[00:00:04.323,211] <inf> rail_manager: Rail manager init (ref-counts 0)
[00:00:04.331,756] <inf> button_counter_store: Counter store loaded: slot=0 seq=4
[00:00:04.331,756] <inf> button_counter_store: Button counters (boot): b0=1 b1=0 b2=0 b3=0 b4=0 b5=8
[00:00:04.338,836] <inf> devnonce_store: DevNonce store loaded (slot=1 seq=489 last=12823)
[00:00:04.340,209] <inf> join_state_store: Join state: has_joined_once = 0
[00:00:04.341,674] <inf> rtc_app: RTC already initialized (2026-02-15 05:27:58)
[00:00:04.345,153] <inf> battery_adc: ADC internal reference voltage: 600 mV
[00:00:04.345,184] <inf> battery_adc: ADC initialized and calibrated
[00:00:04.405,578] <inf> pn5180: PN5180 initialized successfully
[00:00:04.410,827] <inf> nfc_svc: nfc_service initialized (start nfc_worker_id from main)
[00:00:04.410,888] <inf> display_mgr: [EPD] Setting buffers: size=15008 stride=50 mode=DIRECT
[00:00:04.452,392] <inf> display_mgr: display_manager init (EPD enabled, LVGL initialized)
[00:00:04.452,453] <inf> lora_app: SX1262: device ready.
[00:00:04.491,851] <inf> lora_app: Adaptive Data Rate (ADR) enabled
[00:00:04.491,851] <inf> lora_app: LoRaWAN stack initialized successfully.
[00:00:04.491,912] <inf> main: LED thread started
[00:00:04.491,943] <inf> main: NFC worker started
[00:00:04.491,973] <inf> main: LoRa thread started
[00:00:04.492,034] <inf> main: SMF thread started
[00:00:04.492,095] <inf> buttons: Button 0 on gpio@50000000 pin 11
[00:00:04.492,187] <inf> buttons: Button 1 on gpio@50000000 pin 12
[00:00:04.492,248] <inf> buttons: Button 2 on gpio@50000000 pin 24
[00:00:04.492,340] <inf> buttons: Button 3 on gpio@50000000 pin 25
[00:00:04.492,401] <inf> buttons: Button 4 on gpio@50000300 pin 1
[00:00:04.492,492] <inf> buttons: Button 5 on gpio@50000000 pin 28
[00:00:04.492,523] <inf> main: Input thread started
[00:00:04.492,553] <inf> main: Housekeeping thread started
[00:00:04.492,614] <inf> main: Rails released (idle); 3.3V/3.3A/3.6V off until requested
[00:00:04.492,675] <inf> smf: [SMF] thread started, state=Normal
[00:00:04.492,706] <inf> smf: [SMF] system ready (all go)
[00:00:04.492,767] <inf> lora_thread: 
[00:00:04.492,797] <inf> lora_thread: === LORA THREAD ENTRY ===
[00:00:04.492,828] <inf> lora_thread: LoRa thread started - Thread ID: 0x200007c8
[00:00:04.492,828] <inf> lora_thread: LoRa thread priority: 7
[00:00:04.492,858] <inf> lora_thread: LoRa thread stack size: 2048
[00:00:04.492,889] <inf> lora_thread: has_joined_once=0 (EEPROM_JOIN_STATE_CLEAR_ON_BOOT=0)
[00:00:04.492,889] <inf> lora_thread: 
[00:00:04.492,919] <inf> lora_thread: === FIRST BOOT: waiting for join command (Staff + 0+1+2) ===
[00:00:04.492,980] <inf> input: Input thread started (single + combo -> SMF)
[00:00:04.493,041] <inf> led_manager: LED UI thread started
[00:00:04.493,072] <inf> housekeeping: [housekeeping] thread started, jitter=1 offset_min=841
[00:00:07.576,965] <inf> ssd1683: Display initialization completed
[00:00:12.614,624] <inf> display_mgr: [EPD] show LOGO (FeedBackNow FlexBox)
[00:00:12.773,529] <inf> ssd1683: Display initialization completed
[00:02:00.672,851] <inf> input: [Input] Start timing combo: ev=7, held=0x3, last_fired=0x0
[00:02:00.719,207] <inf> input: [Input] btn=1 PRESS, held_before=0x3
[00:02:00.719,207] <inf> input: [Input] session_buttons=0x2 after press
[00:02:00.719,940] <inf> input: [Input] btn=0 PRESS, held_before=0x3
[00:02:00.719,970] <inf> input: [Input] session_buttons=0x3 after press
[00:02:02.674,255] <inf> input: [Input] COMBO FIRED: ev=7, mask=0x3
[00:02:02.674,316] <inf> smf: [SMF] Normal -> Staff (LED solid, 20s timeout)
[00:02:02.674,407] <inf> input: [Input] After combo: session_combo_fired=true, last_fired=0x3
[00:02:03.332,458] <inf> input: [Input] btn=0 RELEASE, held_before=0x2
[00:02:03.400,665] <inf> input: [Input] btn=1 RELEASE, held_before=0x0
[00:02:05.655,639] <inf> input: [Input] Start timing combo: ev=9, held=0x7, last_fired=0x0
[00:02:05.659,027] <inf> input: [Input] btn=2 PRESS, held_before=0x7
[00:02:05.659,027] <inf> input: [Input] session_buttons=0x4 after press
[00:02:05.665,679] <inf> input: [Input] btn=1 PRESS, held_before=0x7
[00:02:05.665,679] <inf> input: [Input] session_buttons=0x6 after press
[00:02:05.681,732] <inf> input: [Input] btn=0 PRESS, held_before=0x7
[00:02:05.681,762] <inf> input: [Input] session_buttons=0x7 after press
[00:02:08.688,354] <inf> input: [Input] COMBO FIRED: ev=9, mask=0x7
[00:02:08.688,446] <inf> smf: [SMF] Staff -> Normal (deliberate join; trigger LoRa join)
[00:02:08.688,507] <inf> lora_thread: Join command received, starting join loop...
[00:02:08.688,598] <inf> lora_thread: 
[00:02:08.688,629] <inf> lora_thread: === STARTING LORA JOIN LOOP ===
[00:02:08.688,781] <inf> input: [Input] After combo: session_combo_fired=true, last_fired=0x7
[00:02:08.692,718] <inf> lora_thread: 
[00:02:08.692,718] <inf> lora_thread: === JOIN ATTEMPT 0 ===
[00:02:08.692,749] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:02:08.692,779] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:02:08.692,840] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:02:08.692,840] <inf> lora_thread: Joining network using OTAA, devNonce: 12824; attempt: 0
[00:02:09.492,187] <inf> input: [Input] btn=0 RELEASE, held_before=0x6
[00:02:09.555,816] <inf> input: [Input] btn=1 RELEASE, held_before=0x0
[00:02:09.556,518] <inf> input: [Input] btn=2 RELEASE, held_before=0x0
[00:02:11.770,202] <inf> display_mgr: [EPD] show CONNECTING
[00:02:11.921,508] <inf> ssd1683: Display initialization completed
[00:02:16.918,945] <err> lorawan: MlmeConfirm failed : Rx 1 timeout
[00:02:16.921,752] <inf> lora_thread: lorawan_join() returned: -116
[00:02:16.921,752] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:02:16.921,783] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:02:19.962,341] <inf> display_mgr: [EPD] show CONNECTING
[00:02:20.111,297] <inf> ssd1683: Display initialization completed
[00:02:26.925,994] <inf> lora_thread: 
[00:02:26.926,025] <inf> lora_thread: === JOIN ATTEMPT 1 ===
[00:02:26.926,025] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:02:26.926,086] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:02:26.926,116] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:02:26.926,116] <inf> lora_thread: Joining network using OTAA, devNonce: 12825; attempt: 1
[00:02:33.388,916] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:02:33.391,723] <inf> lora_thread: lorawan_join() returned: -116
[00:02:33.391,723] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:02:33.391,754] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:02:43.395,935] <inf> lora_thread: 
[00:02:43.395,965] <inf> lora_thread: === JOIN ATTEMPT 2 ===
[00:02:43.395,996] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:02:43.396,026] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:02:43.396,057] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:02:43.396,087] <inf> lora_thread: Joining network using OTAA, devNonce: 12826; attempt: 2
[00:02:49.858,886] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:02:49.861,694] <inf> lora_thread: lorawan_join() returned: -116
[00:02:49.861,694] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:02:49.861,724] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:02:59.865,905] <inf> lora_thread: 
[00:02:59.865,936] <inf> lora_thread: === JOIN ATTEMPT 3 ===
[00:02:59.865,966] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:02:59.865,997] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:02:59.866,027] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:02:59.866,058] <inf> lora_thread: Joining network using OTAA, devNonce: 12827; attempt: 3
[00:03:06.328,826] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:03:06.331,634] <inf> lora_thread: lorawan_join() returned: -116
[00:03:06.331,634] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:03:06.331,665] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:03:16.335,845] <inf> lora_thread: 
[00:03:16.335,876] <inf> lora_thread: === JOIN ATTEMPT 4 ===
[00:03:16.335,876] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:03:16.335,937] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:03:16.335,968] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:03:16.335,968] <inf> lora_thread: Joining network using OTAA, devNonce: 12828; attempt: 4
[00:03:22.798,767] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:03:22.801,544] <inf> lora_thread: lorawan_join() returned: -116
[00:03:22.801,574] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:03:22.801,605] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:03:32.805,786] <inf> lora_thread: 
[00:03:32.805,816] <inf> lora_thread: === JOIN ATTEMPT 5 ===
[00:03:32.805,847] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:03:32.805,877] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:03:32.805,908] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:03:32.805,908] <inf> lora_thread: Joining network using OTAA, devNonce: 12829; attempt: 5
[00:03:39.268,737] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:03:39.271,545] <inf> lora_thread: lorawan_join() returned: -116
[00:03:39.271,545] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:03:39.271,575] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:03:49.275,787] <inf> lora_thread: 
[00:03:49.275,787] <inf> lora_thread: === JOIN ATTEMPT 6 ===
[00:03:49.275,817] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:03:49.275,848] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:03:49.275,878] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:03:49.275,909] <inf> lora_thread: Joining network using OTAA, devNonce: 12830; attempt: 6
[00:03:55.738,830] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:03:55.741,638] <inf> lora_thread: lorawan_join() returned: -116
[00:03:55.741,638] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:03:55.741,668] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:04:05.745,849] <inf> lora_thread: 
[00:04:05.745,880] <inf> lora_thread: === JOIN ATTEMPT 7 ===
[00:04:05.745,910] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:04:05.745,941] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:04:05.745,971] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:04:05.745,971] <inf> lora_thread: Joining network using OTAA, devNonce: 12831; attempt: 7
[00:04:12.208,740] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:04:12.211,547] <inf> lora_thread: lorawan_join() returned: -116
[00:04:12.211,547] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:04:12.211,578] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:04:22.215,759] <inf> lora_thread: 
[00:04:22.215,789] <inf> lora_thread: === JOIN ATTEMPT 8 ===
[00:04:22.215,820] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:04:22.215,850] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:04:22.215,881] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:04:22.215,881] <inf> lora_thread: Joining network using OTAA, devNonce: 12832; attempt: 8
[00:04:28.336,151] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:04:28.338,958] <inf> lora_thread: lorawan_join() returned: -116
[00:04:28.338,958] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:04:28.338,989] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:04:38.343,170] <inf> lora_thread: 
[00:04:38.343,200] <inf> lora_thread: === JOIN ATTEMPT 9 ===
[00:04:38.343,200] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:04:38.343,261] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:04:38.343,292] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:04:38.343,292] <inf> lora_thread: Joining network using OTAA, devNonce: 12833; attempt: 9
[00:04:44.806,060] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:04:44.808,868] <inf> lora_thread: lorawan_join() returned: -116
[00:04:44.808,868] <wrn> lora_thread: Join timed out - will retry in 10 seconds
[00:04:44.808,898] <inf> lora_thread: Sleeping for 10 seconds before retry...
[00:04:54.813,079] <inf> lora_thread: 
[00:04:54.813,110] <inf> lora_thread: === JOIN ATTEMPT 10 ===
[00:04:54.813,140] <inf> lora_thread: LoRaWAN OTAA identifiers in use:
[00:04:54.813,171] <inf> lora_thread: DevEUI
                                      46 4c 58 71 81 32 e9 08                          |FLXq.2..         
[00:04:54.813,201] <inf> lora_thread: JoinEUI
                                      ea f2 42 8a 14 db fd 07                          |..B.....         
[00:04:54.813,201] <inf> lora_thread: Joining network using OTAA, devNonce: 12834; attempt: 10
[00:05:00.311,248] <inf> lorawan: Joined network! DevAddr: 021f5525
[00:05:00.314,025] <inf> lora_app: New Datarate: DR_0, Max Payload 11
[00:05:00.314,056] <inf> lorawan: Datarate changed: DR_0
[00:05:00.314,086] <inf> lora_thread: lorawan_join() returned: 0
[00:05:00.314,086] <inf> lora_thread: 
[00:05:00.314,147] <inf> lora_thread: === LORA JOIN SUCCESSFUL ===
[00:05:00.315,673] <inf> smf: [SMF] counter_sync: queued button 0
[00:05:00.317,016] <inf> join_state_store: Join state: set has_joined_once = 1 (persisted)
[00:05:00.317,047] <inf> time_sync: 
[00:05:00.317,077] <inf> time_sync: === TIME SYNC: requesting network time ===
[00:05:00.318,511] <inf> time_sync: RTC epoch before request: 1771133574
[00:05:00.318,542] <inf> time_sync: LoRaWAN device time not yet available (ret=-11)
[00:05:00.515,838] <inf> smf: [SMF] counter_sync: queued button 1
[00:05:00.716,003] <inf> smf: [SMF] counter_sync: queued button 2
[00:05:00.916,168] <inf> smf: [SMF] counter_sync: queued button 3
[00:05:01.116,333] <inf> smf: [SMF] counter_sync: queued button 4
[00:05:01.316,497] <inf> smf: [SMF] counter_sync: queued button 5
[00:05:01.701,568] <inf> lorawan: DevTimeReq done
[00:05:01.701,629] <inf> lora_app: Port 0, Pending 0, RSSI -44dB, SNR 7dBm, Time 1
[00:05:01.701,660] <inf> lora_app: 
[00:05:01.701,690] <inf> lora_app: === LoRaWAN time updated by network (DeviceTimeAns / clock sync) ===
[00:05:01.704,467] <inf> time_sync: DeviceTimeReq sent; will update RTC on DeviceTimeAns
[00:05:01.704,498] <inf> lora_thread: 
[00:05:01.704,528] <inf> lora_thread: === LORA JOIN LOOP COMPLETED SUCCESSFULLY ===
[00:05:01.704,559] <inf> lora_thread: 
[00:05:01.704,589] <inf> lora_thread: === LORA THREAD ENTERING MESSAGE LOOP ===
[00:05:01.704,620] <inf> lora_thread: Sending payload (port 20, len 11):
[00:05:01.704,681] <inf> lora_thread: 
                                      69 91 5a 86 12 00 00 00  01 00 00                |i.Z..... ...     
[00:05:03.159,454] <inf> lora_app: Port 0, Pending 0, RSSI -44dB, SNR 8dBm, Time 0
[00:05:03.162,261] <inf> lora_thread: Data sent on port 20
[00:05:03.162,292] <inf> lora_thread: Sending payload (port 20, len 11):
[00:05:03.162,322] <inf> lora_thread: 
                                      69 91 5a 86 12 01 00 00  00 00 00                |i.Z..... ...     
[00:05:03.704,589] <inf> time_sync: 
[00:05:03.704,620] <inf> time_sync: === TIME SYNC: DeviceTimeAns received ===
[00:05:03.704,650] <inf> time_sync: LoRaWAN device time (GPS seconds) = 1455168795
[00:05:03.704,650] <inf> time_sync: Converting GPS->UTC->Unix:
[00:05:03.704,681] <inf> time_sync:   - GPS epoch->Unix epoch offset: 315964800 s
[00:05:03.704,711] <inf> time_sync:   - GPS-UTC leap seconds: 18
[00:05:03.704,742] <inf> time_sync:   - Resulting Unix epoch seconds: 1771133577
[00:05:03.704,772] <inf> rtc_app: Programming RTC from epoch=1771133577 -> 2026-02-15 05:32:57 (UTC)
[00:05:03.707,763] <inf> rtc_app: RTC updated from epoch=1771133577 -> 2026-02-15 05:32:57 (UTC)
[00:05:03.707,794] <inf> time_sync: RTC synced from LoRaWAN time (gps=1455168795 -> epoch=1771133577)
[00:05:03.709,197] <inf> time_sync: RTC readback epoch=1771133577 (delta=0 s)
[00:05:04.622,833] <inf> display_mgr: [EPD] show LAST_CLEANED 2026/02/14 21:11
[00:05:04.781,341] <inf> ssd1683: Display initialization completed
[00:05:09.727,416] <inf> lora_app: New Datarate: DR_3, Max Payload 242
[00:05:09.727,447] <inf> lorawan: Datarate changed: DR_3
[00:05:09.727,478] <inf> lora_app: Port 0, Pending 0, RSSI -43dB, SNR 8dBm, Time 0
[00:05:09.730,285] <inf> lora_thread: Data sent on port 20
[00:05:09.730,316] <inf> lora_thread: Sending payload (port 20, len 11):
[00:05:09.730,346] <inf> lora_thread: 
                                      69 91 5a 86 12 02 00 00  00 00 00                |i.Z..... ...     
[00:05:10.815,338] <inf> lora_app: Port 0, Pending 0, RSSI -47dB, SNR 11dBm, Time 0
[00:05:10.818,145] <inf> lora_thread: Data sent on port 20
[00:05:10.818,176] <inf> lora_thread: Sending payload (port 20, len 11):
[00:05:10.818,206] <inf> lora_thread: 
                                      69 91 5a 86 12 03 00 00  00 00 00                |i.Z..... ...     
[00:05:11.903,228] <inf> lora_app: Port 0, Pending 0, RSSI -46dB, SNR 11dBm, Time 0
[00:05:11.906,036] <inf> lora_thread: Data sent on port 20
[00:05:11.906,066] <inf> lora_thread: Sending payload (port 20, len 11):
[00:05:11.906,097] <inf> lora_thread: 
                                      69 91 5a 86 12 04 00 00  00 00 00                |i.Z..... ...     
[00:05:12.991,088] <inf> lora_app: Port 0, Pending 0, RSSI -49dB, SNR 8dBm, Time 0
[00:05:12.993,896] <inf> lora_thread: Data sent on port 20
[00:05:12.993,927] <inf> lora_thread: Sending payload (port 20, len 11):
[00:05:12.993,957] <inf> lora_thread: 
                                      69 91 5a 86 12 05 00 00  08 00 00                |i.Z..... ...     
[00:05:14.078,948] <inf> lora_app: Port 0, Pending 0, RSSI -45dB, SNR 12dBm, Time 0
[00:05:14.081,756] <inf> lora_thread: Data sent on port 20
[00:05:33.380,432] <inf> input: [Input] btn=5 PRESS, held_before=0x20
[00:05:33.380,462] <inf> input: [Input] session_buttons=0x20 after press
[00:05:33.516,784] <inf> input: [Input] btn=5 RELEASE, held_before=0x0
[00:05:33.516,845] <inf> app_logic: [app_logic] public_vote called: button_id=5, now=333516
[00:05:33.518,402] <inf> app_logic: Queued button uplink: btn=5 ctr=9 ts=1771133607
[00:05:33.518,463] <inf> lora_thread: Sending payload (port 10, len 11):
[00:05:33.518,493] <inf> lora_thread: 
                                      69 91 5a a7 00 05 00 00  09 00 00                |i.Z..... ...     
[00:05:34.603,485] <inf> lora_app: Port 0, Pending 0, RSSI -49dB, SNR 9dBm, Time 0
[00:05:34.606,262] <inf> lora_thread: Data sent on port 10
[00:05:36.616,271] <inf> display_mgr: [EPD] show THANKS (5s then last cleaned)
[00:05:36.778,106] <inf> ssd1683: Display initialization completed
[00:05:41.726,318] <inf> button_counter_store: EEPROM counter flush OK (slot=1 seq=5)
[00:05:44.718,170] <inf> display_mgr: [EPD] show LAST_CLEANED 2026/02/14 21:11
[00:05:44.876,220] <inf> ssd1683: Display initialization completed
---

## 2. Disconnected operation (gateway OFF after joined)

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 2.1 | Device **joined** (gateway was ON). Power **OFF** gateway. | — | — |
| 2.2 | Wait until next uplink would have been sent (e.g. housekeeping ~120s) or press a button. | Log. | Button: `Not joined; dropped button id=...`. Counters still increment. |
| 2.3 | Press button 2–3 times (different buttons). | Log each time. | Each: LED + Thanks, “Not joined; dropped”, counter increments. |
| 2.4 | Power **ON** gateway. Wait for join (device retries every 10s if it’s in join loop, or wait for next housekeeping if it only does join on command). | Log. | If join loop is running: eventually `Joined network!`. Then counter-sync (0x07) for each button. |
| 2.5 | If device does **not** auto-rejoin: enter Staff (0+1 hold 2s), then 0+1+2 hold 3s to trigger join. | Log. | Join runs, then counter-sync. |

**Notes:** Does device auto-retry join when it was previously joined and gateway comes back? What does the log show?
1. after joined, i disconnected the device, the logs show that it did send (since our public buttons are unconfirmed,)
2. thse logs are when the gateway was off, on button press
00:10:35.369,720] <inf> app_logic: Queued button uplink: btn=2 ctr=2 ts=1771133909
[00:10:35.369,781] <inf> lora_thread: Sending payload (port 10, len 11):
[00:10:35.369,812] <inf> lora_thread: 
                                      69 91 5b d5 00 02 00 00  02 00 00                |i.[..... ...     
[00:10:37.525,543] <inf> lora_thread: Data sent on port 10
[00:10:38.467,590] <inf> display_mgr: [EPD] show THANKS (5s then last cleaned)
[00:10:38.622,894] <inf> ssd1683: Display initialization completed
[00:10:43.567,810] <inf> button_counter_store: EEPROM counter flush OK (slot=1 seq=9)
[00:10:46.568,237] <inf> display_mgr: [EPD] show LAST_CLEANED 2026/02/14 21:11
[00:10:46.727,050] <inf> ssd1683: Display initialization completed
3. i set jitter=0, this is what happend when it queued hearbeat after it was initally joined and plugged gateway out: 
[00:05:23.495,910] <inf> lora_thread: Data sent on port 20
[00:06:05.335,449] <inf> smf: [SMF] housekeeping: queued battery status 29 mV
[00:06:05.336,944] <inf> smf: [SMF] counter_sync: queued button 0
[00:06:05.537,109] <inf> smf: [SMF] counter_sync: queued button 1
[00:06:05.737,274] <inf> smf: [SMF] counter_sync: queued button 2
[00:06:05.937,438] <inf> smf: [SMF] counter_sync: queued button 3
[00:06:06.137,603] <inf> smf: [SMF] counter_sync: queued button 4
[00:06:06.337,707] <inf> smf: [SMF] counter_sync: queued button 5
[00:06:07.471,771] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:06:07.473,907] <inf> lora_thread: Sending payload (port 20, len 11):
[00:06:07.473,937] <inf> lora_thread: 
                                      69 91 61 5a 10 00 1d 00  00 00 00                |i.aZ.... ...     
[00:06:11.125,579] <err> lorawan: McpsRequest failed : Rx 2 timeout
[00:06:11.127,685] <err> lora_thread: lorawan_send failed: -116
[00:06:11.127,716] <err> lora_thread: Failed to send LoRa message: -116
[00:06:11.127,746] <inf> time_sync: 
[00:06:11.127,777] <inf> time_sync: === TIME SYNC: requesting network time ===
[00:06:11.129,241] <inf> time_sync: RTC epoch before request: 1771135328
[00:06:11.129,272] <inf> time_sync: LoRaWAN device time already available (GPS seconds) = 1455170545
[00:06:13.266,754] <err> lorawan: MlmeConfirm failed : Rx 2 timeout
[00:06:13.268,859] <inf> time_sync: DeviceTimeReq sent; will update RTC on DeviceTimeAns
[00:06:15.269,012] <inf> time_sync: 
[00:06:15.269,073] <inf> time_sync: === TIME SYNC: DeviceTimeAns received ===
[00:06:15.269,073] <inf> time_sync: LoRaWAN device time (GPS seconds) = 1455170549
[00:06:15.269,104] <inf> time_sync: Converting GPS->UTC->Unix:
[00:06:15.269,134] <inf> time_sync:   - GPS epoch->Unix epoch offset: 315964800 s
[00:06:15.269,134] <inf> time_sync:   - GPS-UTC leap seconds: 18
[00:06:15.269,165] <inf> time_sync:   - Resulting Unix epoch seconds: 1771135331
[00:06:15.269,195] <inf> rtc_app: Programming RTC from epoch=1771135331 -> 2026-02-15 06:02:11 (UTC)
[00:06:15.272,399] <inf> rtc_app: RTC updated from epoch=1771135331 -> 2026-02-15 06:02:11 (UTC)
[00:06:15.272,430] <inf> time_sync: RTC synced from LoRaWAN time (gps=1455170549 -> epoch=1771135331)
[00:06:15.273,864] <inf> time_sync: RTC readback epoch=1771135331 (delta=0 s)
[00:06:15.274,017] <inf> lora_thread: Sending payload (port 20, len 11):
[00:06:15.274,047] <inf> lora_thread: 
                                      69 91 61 5a 12 00 00 00  01 00 00                |i.aZ.... ...     
[00:06:17.429,107] <inf> lora_thread: Data sent on port 20
[00:06:17.429,138] <inf> lora_thread: Sending payload (port 20, len 11):
[00:06:17.429,168] <inf> lora_thread: 
                                      69 91 61 5a 12 01 00 00  00 00 00                |i.aZ.... ...     
[00:06:19.584,228] <inf> lora_thread: Data sent on port 20
[00:06:19.584,259] <inf> lora_thread: Sending payload (port 20, len 11):
[00:06:19.584,289] <inf> lora_thread: 
                                      69 91 61 5a 12 02 00 00  02 00 00                |i.aZ.... ...     
[00:06:21.738,372] <inf> lora_thread: Data sent on port 20
[00:06:21.738,403] <inf> lora_thread: Sending payload (port 20, len 11):
[00:06:21.738,433] <inf> lora_thread: 
                                      69 91 61 5a 12 03 00 00  00 00 00                |i.aZ.... ...     
[00:06:23.892,486] <inf> lora_thread: Data sent on port 20
[00:06:23.892,517] <inf> lora_thread: Sending payload (port 20, len 11):
[00:06:23.892,547] <inf> lora_thread: 
                                      69 91 61 5a 12 04 00 00  01 00 00                |i.aZ.... ...     
[00:06:26.047,729] <inf> lora_thread: Data sent on port 20
[00:06:26.047,760] <inf> lora_thread: Sending payload (port 20, len 11):
[00:06:26.047,790] <inf> lora_thread: 
                                      69 91 61 5a 12 05 00 00  0d 00 00                |i.aZ.... ...

So i think we need to make sure we make nfc uplink confirmed, and heartbeats, and couter syncs confirmed, so that we can if it fails, we can concur that the device is not connected with gateway and thus we should try to join again. (the delibrate join is meanted to be used only once when deploying, the resilnce lies in the fact that we see that the confirmed are failing and thus must rejoin, currently it doesnt know and jsut keeps thinking it sent.)


---

## 3. Downlink commands (device joined, gateway ON)

Send downlinks with **first byte = command**. Port is arbitrary (device accepts any port).

### 3.1 EPD update — Last Cleaned (cmd 0x01)

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 3.1.1 | Send **downlink**: 5 bytes `[0x01, E3, E2, E1, E0]` (epoch in **big-endian**). E.g. 1771131600 = `0x69 0x91 0x51 0x90`. | Log + EPD. | `[SMF] cmd 0x01 EPD update epoch=...`, EPD shows “LAST CLEANED” with that time (or next time EPD refreshes). |

**Notes / issues:**
works
---

### 3.2 EPD refresh (cmd 0x02)

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 3.2.1 | Send **downlink**: 1 byte `0x02`. | Log. | `[SMF] cmd 0x02 EPD refresh`, `[EPD] full refresh`. |

**Notes / issues:**
works
---

### 3.3 Status request (cmd 0x04) — trigger heartbeat

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 3.3.1 | Send **downlink**: 1 byte `0x04`. | Log + uplink. | `[SMF] ... status request` (firmware may log cmd as 0x03 — known log bug; you sent 0x04). Shortly after: housekeeping tick, battery/status uplink on port 20. |

**Notes / issues:**
works
---

### 3.4 Reset counters (cmd 0x05)

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 3.4.1 | Note current button counters (e.g. from Device Info or logs). Send **downlink**: 1 byte `0x05`. | Log. | `[SMF] cmd 0x04 reset counters` (log may say 0x04) and `counters reset done`. |
| 3.4.2 | Check counters (Device Info or next button uplink). | Counters. | All buttons 0 (or as per your store implementation). |

**Notes / issues:**
works
---

### 3.5 Timezone offset (cmd 0x03) — 3 bytes

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 3.5.1 | Send **downlink**: 3 bytes `[0x03, high_byte, low_byte]` (int16 minutes, big-endian). E.g. -60 min = 0xFF 0xC4. | Log. | `[SMF] cmd 0x05 timezone offset -60 min` (or similar). EPD “Last Cleaned” may refresh with new TZ. |

**Notes / issues:**
works
---

### 3.6 Factory reset (cmd 0x06)

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 3.6.1 | Send **downlink**: 1 byte `0x06`. | Log + device. | `[SMF] factory reset: has_joined_once cleared`, LED pattern, then **reboot**. Next boot: first boot (no auto-join). |

**Notes / issues:**
works
---

## 4. EPD / display flows

### 4.1 Thanks → Last Cleaned (no cleaning in progress)

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 4.1.1 | Ensure device is in Normal, not in “Cleaning”. | — | — |
| 4.1.2 | Press single button. | EPD sequence. | Thanks 5s, then “Last Cleaned” (with timestamp). |

**Notes / issues:**
works
---

### 4.2 Thanks → Cleaning in progress (persistence)

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 4.2.1 | Enter **Staff** (0+1 hold 2s). Trigger **check-in** (NFC or simulated — if you have check-in implemented, use it; else skip or note “N/A”). | EPD. | If check-in exists: EPD “Cleaning in progress”. |
| 4.2.2 | While “Cleaning in progress” is shown, press a **single public button**. | EPD sequence. | Thanks 5s, then EPD returns to **“Cleaning in progress”** (not “Last Cleaned”). |
| 4.2.3 | Wait 5s after Thanks. | EPD. | Still “Cleaning in progress”. |

**Notes / issues:**
works
---

### 4.3 Downlink 0x01 during Thanks

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 4.3.1 | Press single button (Thanks on EPD). Within 5s send **downlink 0x01** (epoch for Last Cleaned). | Log + EPD after 5s. | After Thanks ends: EPD shows “Last Cleaned” with **downlink** epoch (not overwritten by older store). |

**Notes / issues:**
works
---

## 5. Staff combos and timeouts

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 5.1 | From **Normal**, hold **0+1+2** for 3s (no Staff). | Log. | “COMBO_JOIN ignored (enter Staff first)” or similar. |
| 5.2 | Enter **Staff** (0+1 hold 2s). Wait **10s** without pressing anything. | Log + LED. | Timeout: `Staff -> Normal (timeout)`, LED off. |
| 5.3 | Enter **Staff** again. Hold **0+1+5** for 3s. | Log + EPD. | Device Info (30s timeout). EPD shows DevEUI, counters, etc. |
| 5.4 | Wait 30s in Device Info. | Log. | `DeviceInfo -> Normal (timeout)`. |
| 5.5 | **Reboot combo (careful):** Enter Staff, hold **0+1+2+3** for **10s**. | Log + device. | `Staff -> Reboot`, LED 3s, then device **reboots**. |

**Notes / issues:**
works
---

## 6. Edge cases / stress

| Step | Action | What to log/observe | Expected |
|------|--------|--------------------|----------|
| 6.1 | Rapid button presses (within 5s cooldown). | Log. | Second press: “Vote BLOCKED by cooldown”, no second uplink. |
| 6.2 | Join in progress; press button. | Log. | Button: LED + Thanks; uplink may be sent after join or dropped (note which). |
| 6.3 | Send **invalid/unknown downlink**: e.g. 1 byte `0xFF`. | Log. | `[SMF] unknown downlink cmd 0xFF`, no crash. |
| 6.4 | Send **0x01 with bad length** (e.g. 2 bytes). | Log. | `[SMF] cmd 0x01 EPD update: len 2 < 5` (or similar). |
| 6.5 | Gateway OFF, press button, power gateway ON before 10s. | Log. | Note whether next join succeeds and counter-sync includes that press. |

**Notes / issues:**
yeah it registers cooldown
---

## 7. What to send back

When done (or after each section), please share:

1. **Log excerpts** (paste or attach) for any step where behavior was **wrong** or **unclear**.
2. **Your “Notes / issues”** from the tables (even one-line per step helps).
3. **Unexpected behavior:** e.g. no downlink handling, wrong screen after Thanks, join when it shouldn’t, reboot loop, etc.
4. **Config used:** e.g. `HOUSEKEEPING_INTERVAL_SECONDS`, log level, any `sys_config.h` or overlay changes.

Then we can go through and fix bugs and edge cases step by step.
