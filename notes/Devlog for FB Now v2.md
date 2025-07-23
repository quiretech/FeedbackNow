---
title: Devlog for FB Now v2
created: '2025-07-23T15:08:02.906Z'
modified: '2025-07-23T20:14:38.116Z'
---

## Devlog for FB Now v2

Goal for this document: Track development, learnings, findings in a structured & deliberate way.



7/23/2025

First things first, explore RAK4630.

1. I can send AT commands via a USB connection, UART1, or wirelessly via a BLE connection. Thats good, maybe a BLE app for provision / at+join JIT
2. Had to update the bootloader. Outdated bootloader won't allow you to connect via USB etc. Wisblock app flahsed new firmware RUI3, so all good now. 


Extended Blinky-esque project:
1. get comfy with rui3 api, interfacing onboard leds, IO etc. Idea is to get button IO input to do_something()

1a. OK. After much headbanging, these are my findings:
-- bootloaders/rui3 firmware documentation is a mess. This [write-up](https://docs.rakwireless.com/product-categories/wisblock/rak4631-r/dfu/) worked. The bootloader and nrfutil will be in `RAK4631-R Update/` if anybody reads this log (idk why) and wants to follow along
-- rak4630 needs to be put in "boot" mode by pressing reset button in sucession in order to flash it with ardunio.
-- onboard led1, led2 c an be programmed now 



2. Likely will feed into sending a payload to my lorawan gateway (take a call if onboarding process has to be robust rn or not)

2a. Ok. Just implemented a full cycle of onboarding (euis, appkeys via AT) and periodic uplink of data every 10 seconds w/ the RAK WISGATE & RPI Chirpstack sx12 pihat // Based on rui3. Pretty Straightforward. Next goal would be to have code implementation to generate 8/16 bit on the fly, write using AT and then AT+JOIN and perhaps a python script or something that saves these keys as CSV for fbnows nodeRed pipeline. 




