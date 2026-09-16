/*
 * Compile-time check: DEVICE_HW_VARIANT matches Kconfig (prj_flexbox*.conf).
 * CMake selects the conf fragment from onboarding_config.h before Zephyr init.
 */
#include "sys_config.h"

#if DEVICE_HW_VARIANT == FLEXBOX
	#if defined(CONFIG_SSD1683) 
	#error "FLEXBOX_PLUS_MED build must disable SSD1683"
	#endif
	#if defined(CONFIG_PN5180)
	#error "FLEXBOX_PLUS_MED build must disable PN5180"
	#endif
	#if defined(CONFIG_DISPLAY) 
	#error "FLEXBOX_PLUS_MED build must disable DISPLAY"
	#endif
	#if defined(CONFIG_LVGL)
	#error "FLEXBOX_PLUS_MED build must disable LVGL"
	#endif

#elif DEVICE_HW_VARIANT == FLEXBOX_PLUS
	#if !defined(CONFIG_SSD1683) 
	#error "FLEXBOX_PLUS_MED build must enable SSD1683"
	#endif
	#if !defined(CONFIG_PN5180)
	#error "FLEXBOX_PLUS_MED build must disable PN5180"
	#endif
	#if !defined(CONFIG_DISPLAY) 
	#error "FLEXBOX_PLUS_MED build must enable DISPLAY"
	#endif
	#if !defined(CONFIG_LVGL)
	#error "FLEXBOX_PLUS_MED build must enable LVGL"
	#endif

#elif DEVICE_HW_VARIANT == FLEXBOX_PLUS_MED
	#if !defined(CONFIG_SSD1683) 
	#error "FLEXBOX_PLUS_MED build must enable SSD1683"
	#endif
	#if !defined(CONFIG_PN5180)  // This is not right here, need to revist and make sure PN5180 is disabled for FLEXBOX_PLUS_MED build.
	#error "FLEXBOX_PLUS_MED build must disable PN5180"
	#endif
	#if !defined(CONFIG_DISPLAY) 
	#error "FLEXBOX_PLUS_MED build must enable DISPLAY"
	#endif
	#if !defined(CONFIG_LVGL)
	#error "FLEXBOX_PLUS_MED build must enable LVGL"
	#endif

#endif
