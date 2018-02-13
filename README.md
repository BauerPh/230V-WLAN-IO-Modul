# 230V-WLAN-IO-Modul
This ESP8266 Firmware is developed to run on the *ESP8266 230V I/O Modul* developed by **luani**.<br>
You can find luanis project page here: https://luani.de/projekte/esp8266-hvio

The Webserver of this Firmware is based on *FSBrowserNG* by **Germán Martín**: https://github.com/gmag11/FSBrowserNG

For now the language of the Webserver is German.
## Table of contents
- [230V-WLAN-IO-Modul ](#230v-wlan-io-modul)
  - [Functions](#functions)
  - [Installation using precompiled binaries](#installation-using-precompiled-binaries)
  - [First Configuration](#first-configuration)
  - [Setup your own firmware update server](#setup-your-own-firmware-update-server)
  - [Using the sourcecode](#using-the-sourcecode)
    - [Dependencies](#dependencies)

## Functions
- Configurable via built-in asynchronous Webserver
  - built-in SPIFFS file editor, so you can edit all html, css, js,... files online!
- NTP Time synchronisation
- Full asynchronous MQTT support
  - publish output state
  - publish input changes
  - subscribe to topic to control outputs
  - select *qos* and *retain* for each publish or subscribe separately
- Automatic Firmware Update
  - MQTT update trigger topic configurable
- Configurable input-output-dependencies
- Config is stored on SPIFFS
 
## Installation using precompiled binaries
1. Download [nodemcu-flasher](https://github.com/nodemcu/nodemcu-flasher/raw/master/Win32/Release/ESP8266Flasher.exe "NODEMCU FIRMWARE PROGRAMMER")
2. Download latest [release](https://github.com/BauerPh/230V-WLAN-IO-Modul/releases)
   - use `ESP_230V_IO_UP_2O.bin` and `ESP_230V_IO_UP_2O_SPIFFS.bin` for the *ESP8266 230V I/O Modul* by luani
3. Open *nodemcu-flasher*
4. Connect your ESP and bring it in flash mode
5. Configure Nodemcu Firmware Programmer:<br>
![nodemcuflasher_advanced](readme/nodemcuflasher_advanced.jpg "Advanced")
6. Select binaries downloaded in Step 2 and don't forget to tick the two boxes!:<br>
![nodemcuflasher_advanced](readme/nodemcuflasher_config.jpg "Config")
7. Select COM-Port and press *Flash*:<br>
![nodemcuflasher_advanced](readme/nodemcuflasher_operation.jpg "Operation")
8. Thats it! Restart ESP and try to connect to it's WiFi Hotspot!

## First configuration
TODO

## Setup your own firmware update server
TODO

## Using the sourcecode
The best way is to use Visual Micro Extension for Microsoft Visual Studio

### Dependencies
- `FSWebServerLib` Async Webserver Library by **BauerPh** https://github.com/BauerPh/FSWebServerLib
- `JSONtoSPIFFS` JSON to SPIFFS Library by **BauerPh** https://github.com/BauerPh/JSONtoSPIFFS
- `Time` Arduino Time Library by **Paul Stoffregen** https://github.com/PaulStoffregen/Time
- `NtpClient` NTP Client Library by **Germán Martín** https://github.com/gmag11/NtpClient
- `ESPAsyncTCP` Async TCP Library by **Me No Dev** https://github.com/me-no-dev/ESPAsyncTCP
- `ESPAsyncWebServer` Async HTTP and WebSocket Server by **Me No Dev** https://github.com/me-no-dev/ESPAsyncWebServer
- `ArduinoJson` JSON Library by **Benoît Blanchon** https://github.com/bblanchon/ArduinoJson