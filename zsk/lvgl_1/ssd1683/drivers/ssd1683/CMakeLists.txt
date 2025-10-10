# SSD1683 driver CMake
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_SSD1683 ssd1683.c)
zephyr_library_include_directories(../../include)
