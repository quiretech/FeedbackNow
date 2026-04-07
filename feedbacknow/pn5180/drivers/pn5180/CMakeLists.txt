# PN5180 driver sources
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_PN5180 pn5180.c)
zephyr_library_include_directories(../../include)