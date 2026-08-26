---
title: Devlog for FB Now v2
created: '2025-07-23T15:08:02.906Z'
modified: '2025-08-05T22:16:25.496Z'
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




7/31/2025

Updates: We might phase out rak4630 from the design and rather use NRF + SX12XX as our own rak4630 to combat supply chain issues. nrf,sx are much more readily available than standlone RAK. ofcourse this mean lots of custom firwamre and things from scratch, which is cool ngl. This also means to do away with milesights eink paper dispaly module, and create something of our own. I like their nfc ecoysstem to apply templates in situ, my nrf has nfc, so perhaps use that??! that'll be cool.

This week has been me tinkering with waveshare's 4.2 epaper G display. A quick test with RPI (w/ hat) gave out good results. I can put my custom text, move it around put custom images etc etc. I would like it to be more robust and "template" (something like a CSS designed webpage with dynamic / static fields where my code comes in later)

Before that, i think its time for me to actually understand how these displays work. I know they are made up of small "ink balls" that get excited when applied V but not sure of the tech specific knowledge. So, this calls for a detour:


What's E-paper?
a. Polarity of charge flips microcapsules (filled with titanium) from black to white
b. amazing battery life cz it once displayed, it can go to sleep (no continous power to display)


When it comes to waveshare, there are couple of things to be sure of: (from website, come back to this when writing driver for nrf)

-- For e-Paper displays that support partial refresh, please note that you cannot refresh them with the partial refresh mode all the time. After refreshing partially several times, you need to fully refresh EPD once. Otherwise, the display effect will be abnormal, which cannot be repaired!
-- Note that the screen cannot be powered on for a long time. When the screen is not refreshed, please set the screen to sleep mode or power off it. Otherwise, the screen will remain in a high voltage state for a long time, which will damage the e-Paper and cannot be repaired!
-- When using the e-Paper display, it is recommended that the refresh interval is at least 180s, and refresh at least once every 24 hours. If the e-Paper is not used for a long time, you should use the program to clear the screen before storing it. (Refer to the datasheet for specific storage environment requirements.)


8/3 - 8/4

I started with nrf52840dk w/ zephyr. Set up west, 3.0.2 toolchain and nrfsdk. First goal is to interface the dispaly using nrf.

I find zephyr supports the universal shield for EPD by wavesahre and some other displays. The limiting factor is the driver IC, which unfortunately isn't what I have. I have wavesahre epd4.2g 4 color, with Driver IC: JD79668.

What can I do:
1. spend sometime to run the example code anyway
2. order the exact one that zephyr supports to begin with -- this is what we did


7 Button I/O Sketch Startup:

1. repurposed button press sketch to interface on board led w/ 4 + P0.28 GPIO w/ zephyr


8/5
1. got the nrf52840DK, Sample App lorawan class A, working. NOTE: for SX1262 mbed shield, the max SPI has to be changed from 16Mhz to 8Mhz for it work.
2. The sample app doesn't save the DEVNonce onto NVS, because of which the network server won't allow the joinRequest all the time. So the nonce has to be stored on the NVS, and counted up everytime it joins OR create a random number, and then access it from NVS.

2a. NVS sample App Learning. Storing and retrrieving data from NVS.
-- done: what i learnt:
1. stores things w/ key_ID.

Next, I ported code to class_a code, now i create random app keys, store on nvs, use the last devNonce value and val++, (so theres no collisison). 
Next step would be to reduce the nvs partition size

//  nk 08/15

if the last cleaned display is replaced by the logo after preset timeout, it will not be able to display 
cleaned forced time through downlink, unless I reset the timeout 













