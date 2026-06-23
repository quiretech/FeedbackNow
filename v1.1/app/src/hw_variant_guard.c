/*
 * Compile-time check: DEVICE_HW_VARIANT matches Kconfig (prj_flexbox*.conf).
 * CMake selects the conf fragment from onboarding_config.h before Zephyr init.
 */
#include "sys_config.h"

#if DEVICE_HW_VARIANT == FLEXBOX
#if defined(CONFIG_SSD1683) || defined(CONFIG_PN5180) || defined(CONFIG_DISPLAY) || \
	defined(CONFIG_LVGL)
#error "FLEXBOX build must not enable SSD1683, PN5180, DISPLAY, or LVGL"
#endif
#elif DEVICE_HW_VARIANT == FLEXBOX_PLUS
#if !defined(CONFIG_SSD1683) || !defined(CONFIG_PN5180) || !defined(CONFIG_DISPLAY) || \
	!defined(CONFIG_LVGL)
#error "FLEXBOX_PLUS build must enable SSD1683, PN5180, DISPLAY, and LVGL"
#endif
#endif
