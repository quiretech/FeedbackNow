# FlexBox v1.2 — Build and production
# 

## Layout

- **Headers:** `include/` — `sys_config.h` (firmware tunables), `onboarding_config.h` (unit/registry, gen_euis.py), `sys_config_profile.h` (PRODUCTION/DESK/LAB)
- **Sources:** `src/` (`.c` only)

## Build and flash

From repo root or `app/`:

```text
west build -b nrf52840dk/nrf52840
west flash --runner nrfjprog
```

CMake reads `include/onboarding_config.h` **`DEVICE_HW_VARIANT`** and automatically:

| Variant | Kconfig fragment | Devicetree | Out-of-tree drivers |
|---------|------------------|------------|---------------------|
| `FLEXBOX` | `prj_flexbox.conf` (no EPD/NFC/LVGL) | base overlay only | none |
| `FLEXBOX_PLUS` | `prj_flexbox_plus.conf` | base + `boards/nrf52840dk_nrf52840_flexbox_plus.overlay` | `ssd1683`, `pn5180` |

App code uses `EPD_ENABLED` / `NFC_ENABLED` from the same header. `src/hw_variant_guard.c` fails the build if Kconfig and header disagree.

Check consistency: `python onboarding/provision.py validate`

Serial console: 115200 8N1.

## Production build (default)

- **Path:** Real buttons only. Input → SMF → app logic; no demo/pseudo simulation.
- **Logging:** Default log level is INFO for `LOG_STATE` / `LOG_EVT`; WRN/ERR always visible. Per-event detail remains DBG (enable with `CONFIG_LOG_DEFAULT_LEVEL=4`).
- **EEPROM probe:** At boot, one line "EEPROM probe OK" unless `SYS_CONFIG_EEPROM_PROBE_LOG=1` (set in `sys_config_profile.h` for LAB/DESK/PRODUCTION).

## Bring-up / debug

- **Subsystem visibility (default):** `CONFIG_LOG_DEFAULT_LEVEL=3` in `prj.conf` prints `LOG_STATE` (SMF, input, EPD, LoRa, NFC) and `LOG_EVT` (telemetry) plus WRN/ERR. DBG detail is compiled out.
- **Full trace:** Set `CONFIG_LOG_DEFAULT_LEVEL=4` in `prj.conf` (or a local overlay) for GPIO masks, EPD timings, join MAC steps.
- **ERR-only field:** `CONFIG_LOG_DEFAULT_LEVEL=1`.
- **Verbose Input and SMF:** Requires level 4 (DBG) — see above.
- **EEPROM hex at boot:** Set `SYS_CONFIG_EEPROM_PROBE_LOG 1` in `sys_config_profile.h` (or override in a profile block).

## Config headers

| File | Purpose |
|------|---------|
| `sys_config_profile.h` | **One knob:** `SYS_CONFIG_PROFILE` → PRODUCTION, DESK, or LAB (RTC preserve, fast HK/join backoff in LAB, EEPROM test flags). |
| `onboarding_config.h` | Unit id, provision UTC, `DEVICE_HW_VARIANT` (→ `EPD_ENABLED`), registry strings. |
| `sys_config.h` | LoRa, buttons, LED, NFC, RTC, thread sizes, versions. |
| `eeprom_layout.h` | Fixed EEPROM offsets (do not change in the field). |

Factory-reset-on-boot flags are coupled in `sys_config_profile.h` per profile (default 0 for all profiles today).
