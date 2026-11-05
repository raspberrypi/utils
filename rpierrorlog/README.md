
# rpierrorlog

The Raspberry Pi Error Log service is a mailbox based API for reading
and clearing error logs stored in SPI EEPROM. These logs are written 
by the firmware when a fatal failure occurs and persist across reboots.

Although this service can be used via raw vcmailbox commands the
recommended API is either the command line rpi-error-log application
or the librpierrorlog.so shared library.

**Build Instructions**
Install prerequisites with "sudo apt install cmake" - you need at least version 3.10.

 - *mkdir build*
 - *cd build*
 - *cmake ..*
 - *make*
 - *sudo make install*

**Usage**

* rpi-error-log        (Shows usage information)
* rpi-error-log get    (Read and print all EEPROM error log entries - Pi 4 and Pi 5 family boards only)
* rpi-error-log clear  (Clear the EEPROM error log and verify it is empty - Pi 5 family boards only)
