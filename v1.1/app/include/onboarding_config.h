#ifndef ONBOARDING_CONFIG_H
#define ONBOARDING_CONFIG_H

/*
 * Per-unit and per-deployment values for onboarding/gen_euis.py and firmware.
 *
 * DEVICE_HW_VARIANT selects FLEXBOX vs FLEXBOX_PLUS (sys_config.h derives
 * EPD_ENABLED). CMake reads this header and appends prj_flexbox.conf or
 * prj_flexbox_plus.conf, devicetree overlay, and driver modules.
 *
 * FLEXBOX_PLUS:     DEVICE_HW_VARIANT FLEXBOX_PLUS — EPD + NFC enabled at build time.
 * FLEXBOX:          DEVICE_HW_VARIANT FLEXBOX — no EPD/NFC drivers or devicetree nodes.
 * FLEXBOX_PLUS_MED: DEVICE_HW_VARIANT FLEXBOX_PLUS_MED — EPD enabled at build time.
 */

/** Product line — mirrored in registry CSV hw_profile and AWS tag Variant. */
#define FLEXBOX 0
#define FLEXBOX_PLUS 1
#define FLEXBOX_PLUS_MED 2


#define DEVICE_HW_VARIANT FLEXBOX_PLUS
/* =============================================================================
 * Unit identity (written by onboarding/gen_euis.py — matches eui_registry)
 * =============================================================================
 */
/* BEGIN UNIT_ID (gen_euis.py) — do not edit by hand */
#define DEVICE_UNIT_ID_STRING "DEMO-0023"
/* END UNIT_ID (gen_euis.py) */

/* Last provisioning stamp (UTC) from onboarding/gen_euis.py (--stamp-provision-only
 * or full run). rtc.c seeds RTC from this when the chip is invalid, at the
 * factory/test default (RTC_SET_*), or still before this stamp (PRODUCTION).
 * DESK/LAB overwrite every boot. If 0, RTC falls back to RTC_SET_* in sys_config.h. */
/* BEGIN PROVISION_UTC (gen_euis.py) — do not edit by hand */
#define DEVICE_PROVISION_UNIX_UTC 1789510789ULL
#define DEVICE_PROVISION_ISO8601_UTC "2026-09-15T22:19:49Z"
/* END PROVISION_UTC (gen_euis.py) */

/* =============================================================================
 * Registry / AWS onboarding (read by gen_euis.py for new CSV rows)
 * name_prefix → CSV name_prefix; client → tag_client; decal → decal_type (AWS Decal).
 * =============================================================================
 */
#define DEVICE_REGISTRY_NAME_PREFIX_STRING "EWR"

#define DEVICE_REGISTRY_CLIENT_NAME "EWR Terminal A - Munich"

#define DEVICE_REGISTRY_DECAL_TYPE "Restroom"

#endif /* ONBOARDING_CONFIG_H */