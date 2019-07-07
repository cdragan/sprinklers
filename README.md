Introduction
============

This is a sprinkler controller, which can be configured and manipulated
via a web interface over the local network (LAN).

The project uses a NodeMCU v1.0 development board.  This board has an
Espressif ESP-12E board mounted on it, which has an ESP8266EX microcontroller,
which features WiFi.

The board uses 5V input voltage from USB or from a 5V DC power supply.
It is hooked up to relays, which control solenoid valves.  Each solenoid
valve lets water in to a separate zone of the sprinkler system.  The solenoid
valves typically take 24V AC as input, operated by the relays.

After the board boots up, it offers a web interfact over the local network
through which one can connect to with a browser to set up and control
the sprinklers.


NodeMCU v1.0 specification
==========================

* Contains a ESP-12E module with ESP8266EX microcontroller
* CP2102 USB module for communication with host or for power (optional)
* 3.3V regulator for controlling various devices
* 4MB flash of which 3MB is usable
* 32-bit Tensilica Xtensa CPU at 80MHz


Electrical connections
======================

Here is the zone, LED and button assignment on the NodeMCU v1.0 board:

                    +----------+
                    | A0    D0 | GPIO16
                    | RSV   D1 | GPIO5
                    | RSV   D2 | GPIO4
    Zone 5 - GPIO10 | SD3   D3 | GPIO0  - Red LED (error indicator)
              GPIO9 | SD2   D4 | GPIO2  - Green LED (status good)
                    | SD1  3v3 |
                    | CMD  GND |
                    | SD0   D5 | GPIO14 - Zone 1
                    | CLK   D6 | GPIO12 - Zone 2
                    | GND   D7 | GPIO13 - Zone 3
                    | 3v3   D8 | GPIO15 - Zone 6
                    | EN    D9 | GPIO3  - Zone 4
                    | RST  D10 | GPIO1
                    | GND  GND |
                    | Vin  3v3 |
                    +---|USB|--+

* To supply power you can use one of the following approaches:
    - Use USB.
    - Put 5V on the Vin PIN and ground on the GND pin next to it.
    - Under no circumstances attempt to supply power through both methods
      simultaneously, this will damage the board!
* The 3 remaining 3v3 and GND pairs can be used as reference voltage to power
  stuff outside of the board.
* GPIO9 is unusable, when this pin is switched to GPIO on the MUX, the board
  keeps rebooting.
* GPIO1 is used for UART TX bit and so it is unusable, unless we wanted to lose
  the ability to use UART.
* GPIO16 looks like it is unusable (but I may be wrong).
* GPIOs 5, 4, 0, 2 have LOW state after boot and `gpio_init()` is called.
  The remaining GPIOs have HIGH state.
* GPIO2 is connected to the built-in LED (mounted close to the GPIO16 output pin).
  This built-in LED is lit when GPIO2 is in LOW state (the default after
  `gpio_init()`) and it is not lit when GPIO2 is in HIGH state.
* GPIO0 must not be pulled low or the board won't boot.  It is used to indicate
  boot mode during boot, after boot it can be used for anything.
* GPIO15 momentarily comes up in LOW state right after boot and then goes to HIGH.
  This takes something like 500ms.  Because of this, we use it for zone 6,
  which is the least likely to be used.


Physical Interface Design
=========================

* WPS button, when pressed, the system enters a WPS mode and searches WiFi
  networks to connect to.
  - When the system is first installed, it is not connected to any network.
    This causes the red LED to blink.  The user presses a WPS button
    on their WiFi router and then the WPS button connected to this controller.
    After the controller establishes a connection with a WiFi network and obtains
    IP, the green LED lights up and the red LED turns off (unless there is another
    error).
* Upload FS button.
  - When pressed, the system allows uploading (and overwriting)
    the filesystem used to store the web page resources.  Normally it is not
    possible to upload the filesystem, unless this button is held pressed.
* Green LED.
  - This LED lights up continuously if everything is OK, i.e. the system
    has obtained an IP and there are no errors.
  - The green LED blinks slowly if everything is OK but the user has suspended
    the system via the web interface.
  - During boot, the green LED blinks quickly until the system obtains IP and
    time from NTP.  If the system is unable to obtain IP and/or time after 30
    seconds, the green LED goes off and the red LED comes on to show the
    relevant error.
  - The green LED is off if the system cannot obtain IP or if there is any error,
    including a problem with obtaining time from NTP.
* Red LED.
  - This LED lights up continuously if there is any problems occurred, for example
    the system is not able to get time from NTP.
  - The red LED blinks only if it is unable to obtain IP from the local network.
  - The green and the red LEDs are exclusive.  Only one of them is active at any
    given time.


Installing the software
=======================

1. Install CP210x USB to UART bridge driver from Silicon Labs.

2. Install [PlatformIO](https://platformio.org) and framework-esp8266-nonos-sdk.

3. Compile the firmware:

        make

4. Connect ESP8266 over USB, then flash the firware:

        make upload

5. After the ESP8266 boots and obtains IP, upload the files
   (replace sprinklers.local with actual IP if needed):

        make upload_fs ip=sprinklers.local

The GNU Makefile is set up for Mac, but probably few tweaks are needed to make
it work on Linux or Windows.  Check out the extra targets inside the Makefile.


Web UI Design
=============

Main Pane / Home
----------------

* Status: Textual description with relevant color.
    - Active (blue) - one of the zones is active - show name of the active zone
    - Enabled (green)
    - Disabled (gray)
        + The system has been temporarily disabled by the user
    - Error (red)
* Info: More information about the current status.
    - (Active/blue) Which zone is being watered (name of the zone).
    - (Enabled/green) When the next watering is scheduled.
    - (Disabled/gray) When the system was disabled.
    - (Error/red) The description of the problem and information how to fix it.
* Information when the last watering occurred.
* Button to enable/disable the system.  When the system is active (watering),
  hitting the disable button will also stop the watering.
* Button to skip (or unskip) the next watering.  This uses the suspend
  function (see Schedule).

Zones
-----

* Buttons with names of each zone.  Each button can be pressed to turn on
  that specific zone.
* List of zones and zone configuration.  For each zone:
    - Zone number.
    - Zone name.  The user can click the name to edit it.
    - Button to enable/disable zone.  Initially (after installation) all zones
      are disabled.
    - Reorder zones.  Zones will be wattered in their order.
    - Edit watering time (duration) for each zone (in minutes).

Schedule
--------

* Every N days at HH:MM.
* Specific days of the week, with HH:MM specified invididually for each day.
    - Optionally specify the duration for each day.
* Suspend the system for N days.

Configuration
-------------

* Time zone (+1, -8, etc.).
* mDNS name.
* NTP server hostname.

Watering Log
------------

* Times when each zone was on and for how long.
* Boot times and reasons.
* There is a daily limit to how many entries can be logged.

System Info
-----------

* Reset reason.
* IP.
* MAC address.
* Current time.
* Current timezone.
* SDK version.
* Heap free.
