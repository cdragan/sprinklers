Installation
============

1. Install [PlatformIO](https://platformio.org) and framework-esp8266-nonos-sdk.

2. Compile the firmware:

        make

3. Connect ESP8266 over USB, then flash the firware:

        make upload

4. After the ESP8266 boots and obtains IP, upload the files
   (replace sprinklers.local with actual IP if needed):

        make upload_fs ip=sprinklers.local


Web UI
======

* Zone configuration:
    - Give names to each zone.
    - No name for a zone means it is unused (it won't be shown or used).
    - Set individual durations for each zone.  Each zone runs one at a time.
      If duration is 0, the zone is disabled.

* Time configuration:
    - Set/adjust time zone and daylight saving dates.
    - Choose how often watering occurs.  For example: daily or every N days or
      on specific days of the week.
    - Set start time.  If watering is set for days of the week, set time
      individually for each day.
    - Enable/disable the system.
    - Disable the system for N days.

* Manual controls:
    - Manually turn each valve on and off.

* System info: SDK version, memory usage, current time, etc.

* Log: shows times when each zone was running.


Connections
===========

* 1 green LED to indicate good status - GPIO2 which is also connected to built-in LED
* 1 red LED to indicate bad status
    - No IP - red blinking
    - No time from NTP - red continuous
    - Anything else - red continuous
* 1 button for WPS
* 1 button for uploading FS
* N output GPIOs steering relays

