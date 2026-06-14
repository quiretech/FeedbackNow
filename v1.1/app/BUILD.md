# FlexBox v1.2 — Build and production

## Layout

- **Headers:** `include/` — `sys_config.h` (firmware tunables), `onboarding_config.h` (unit/registry, gen_euis.py), `sys_config_profile.h` (PRODUCTION/DESK/LAB)
- **Sources:** `src/` (`.c` only)

## Build and flash

From repo root or `app/`:

```text
west build -b nrf52840dk/nrf52840
west flash --runner nrfjprog
```

Serial console: 115200 8N1.

## Production build (default)

- **Path:** Real buttons only. Input → SMF → app logic; no demo/pseudo simulation.
- **Logging:** Default log level is INFO. Mode transitions (e.g. Normal → Staff, Staff → Normal) and errors are at INFO; per-event and combo details are at DBG.
- **EEPROM probe:** At boot, one line "EEPROM probe OK" unless `SYS_CONFIG_EEPROM_PROBE_LOG=1` (set in `sys_config_profile.h` for LAB/DESK/PRODUCTION).

## Bring-up / debug

- **Verbose Input and SMF:** In `prj.conf` set `CONFIG_LOG_DEFAULT_LEVEL_DBG=4` (or use module-level override) to see all `[Input]` and `[SMF]` traces.
- **EEPROM hex at boot:** Set `SYS_CONFIG_EEPROM_PROBE_LOG 1` in `sys_config_profile.h` (or override in a profile block).

## Config headers

| File | Purpose |
|------|---------|
| `sys_config_profile.h` | **One knob:** `SYS_CONFIG_PROFILE` → PRODUCTION, DESK, or LAB (RTC preserve, fast HK/join backoff in LAB, EEPROM test flags). |
| `onboarding_config.h` | Unit id, provision UTC, `DEVICE_HW_VARIANT` (→ `EPD_ENABLED`), registry strings. |
| `sys_config.h` | LoRa, buttons, LED, NFC, RTC, thread sizes, versions. |
| `eeprom_layout.h` | Fixed EEPROM offsets (do not change in the field). |

Factory-reset-on-boot flags are coupled in `sys_config_profile.h` per profile (default 0 for all profiles today).
