# SSD1683 E-Paper Display Driver
zephyr_library()

# Low-level driver (always needed)
zephyr_library_sources(ssd1683.c)

# Zephyr display API wrapper (when using as Zephyr display driver)
zephyr_library_sources_ifdef(CONFIG_SSD1683 ssd1683_display.c)

zephyr_library_include_directories(../../include)
