#ifndef ONBOARDING_CONFIG_H
#define ONBOARDING_CONFIG_H

/*
 * Per-unit and per-deployment values for onboarding/gen_euis.py and firmware.
 *
 * DEVICE_HW_VARIANT selects FLEXBOX vs FLEXBOX_PLUS (sys_config.h derives
 * EPD_ENABLED). gen_euis.py updates UNIT_ID and PROVISION_UTC markers only.
 *
 * FLEXBOX_PLUS: DEVICE_HW_VARIANT FLEXBOX_PLUS, CONFIG_PN5180=y, display in prj.conf.
 * FLEXBOX:      DEVICE_HW_VARIANT FLEXBOX — no EPD/NFC in firmware; omit CONFIG_PN5180
 *               in prj.conf to save flash if desired.
 */

/** Product line — mirrored in registry CSV hw_profile and AWS tag Variant. */
#define FLEXBOX 0
#define FLEXBOX_PLUS 1


#ifndef DEVICE_HW_VARIANT
#define DEVICE_HW_VARIANT FLEXBOX_PLUS
#endif

/* =============================================================================
 * Unit identity (written by onboarding/gen_euis.py — matches eui_registry)
 * =============================================================================
 */
/* BEGIN UNIT_ID (gen_euis.py) — do not edit by hand */
#define DEVICE_UNIT_ID_STRING "UNIT-0355"
/* END UNIT_ID (gen_euis.py) */

/* Last provisioning stamp (UTC) from onboarding/gen_euis.py (--stamp-provision-only
 * or full run). rtc.c applies DEVICE_PROVISION_UNIX_UTC on every boot when
 * RTC_SET_TIME_ON_BOOT=1 and RTC_PRESERVE_EXISTING_ON_BOOT=0 (desk profile).
 * If 0, RTC falls back to RTC_SET_YEAR/... in sys_config.h. */
/* BEGIN PROVISION_UTC (gen_euis.py) — do not edit by hand */
#define DEVICE_PROVISION_UNIX_UTC 1780453318ULL
#define DEVICE_PROVISION_ISO8601_UTC "2026-06-03T02:21:58Z"
/* END PROVISION_UTC (gen_euis.py) */

/* =============================================================================
 * Registry / AWS onboarding (read by gen_euis.py for new CSV rows)
 * name_prefix → CSV name_prefix; client → tag_client; decal → decal_type (AWS Decal).
 * =============================================================================
 */
#ifndef DEVICE_REGISTRY_NAME_PREFIX_STRING
#define DEVICE_REGISTRY_NAME_PREFIX_STRING "BFS"
#endif

#ifndef DEVICE_REGISTRY_CLIENT_NAME
#define DEVICE_REGISTRY_CLIENT_NAME "Belfast International Airport"
#endif

#ifndef DEVICE_REGISTRY_DECAL_TYPE
#define DEVICE_REGISTRY_DECAL_TYPE "Restroom"
#endif

#endif /* ONBOARDING_CONFIG_H */
