
# rpi-eeprom-ab

The Raspberry Pi EEPROM AB service is a mailbox based API
that allows you to update and manage the AB EEPROM partitions.

Although this service can be used via raw vcmailbox commands the
recommended API is the command line rpi-eeprom-ab application.

## Build Instructions

- *mkdir build*
- *cd build*
- *cmake ..*
- *make*
- *sudo make install*
