# piolib

PIOlib/libPIO is a user-space API to the rp1-pio driver, which gives access to the PIO hardware of RP1. It takes the form of a clone of the PICO SDK PIO API, where most of the methods are implemented as RPC calls to RP1. This allows existing PIO code to be run without (much) alteration, but runs into problems when it relies on the PIO state machine and the support code being closely coupled.

To build piolib:
1. cmake . (or create a build subdirectory and cmake ..)
2. make

If `ls -l /dev/pio0` reports that the file is not found, you may need to update your Pi 5 firmware to one with PIO support and make sure that you are running a suitably recent kernel.
If `ls -l /dev/pio0` reports that the file is owned by `root` and group `root`, you should add the following to /etc/udev/rules/99-com.rules:
```
SUBSYSTEM=="*-pio", GROUP="gpio", MODE="0660"
```

Examples:

* piotest:
    This is the normal WS2812 example LED PIO code, but using DMA to send the data. The optional parameter is the GPIO number to drive; the defailt is 2.
* piopwm:
    The PWM example, unmodified except for dynamic SM allocation and a command line parameter to choose the GPIO. The optional parameter is the GPIO number to drive; the default is 4.
* piows2812:
    The ws2812 example, unmodified except for dynamic SM allocation and a command line parameter to choose the GPIO. The optional parameter is the GPIO number to drive; the defailt is 2.
* rp1sm:
    Show the state of the hardware for a particular SM. The parameter is the number of the state machine to inspect.
* dpi_interlace:
    Nick's interlaced sync fixer. More of an example than something actually usable right now - it may eventually be built into a kernel driver. Run with "-n" or "--ntsc" to enable NTSC timing (it's PAL by default=)

Known issues:
* Blocking operations block the whole RP1 firmware interface until they complete.
