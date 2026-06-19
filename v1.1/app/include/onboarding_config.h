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
#define DEVICE_UNIT_ID_STRING "DEMO-0015"
/* END UNIT_ID (gen_euis.py) */

/* Last provisioning stamp (UTC) from onboarding/gen_euis.py (--stamp-provision-only
 * or full run). rtc.c seeds RTC from this when the chip is invalid, at the
 * factory/test default (RTC_SET_*), or still before this stamp (PRODUCTION).
 * DESK/LAB overwrite every boot. If 0, RTC falls back to RTC_SET_* in sys_config.h. */
/* BEGIN PROVISION_UTC (gen_euis.py) — do not edit by hand */
#define DEVICE_PROVISION_UNIX_UTC 1781833501ULL
#define DEVICE_PROVISION_ISO8601_UTC "2026-06-19T01:45:01Z"
/* END PROVISION_UTC (gen_euis.py) */

/* =============================================================================
 * Registry / AWS onboarding (read by gen_euis.py for new CSV rows)
 * name_prefix → CSV name_prefix; client → tag_client; decal → decal_type (AWS Decal).
 * =============================================================================
 */
#ifndef DEVICE_REGISTRY_NAME_PREFIX_STRING
#define DEVICE_REGISTRY_NAME_PREFIX_STRING "TEST"
#endif

#ifndef DEVICE_REGISTRY_CLIENT_NAME
#define DEVICE_REGISTRY_CLIENT_NAME "TEST"
#endif

#ifndef DEVICE_REGISTRY_DECAL_TYPE
#define DEVICE_REGISTRY_DECAL_TYPE "TEST"
#endif

#endif /* ONBOARDING_CONFIG_H */
