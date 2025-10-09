# Utilities to create, flash and dump HAT EEPROM images

**Build Instructions**

Install the prerequisites with "sudo apt install cmake" - you need at least version 3.10 of cmake. Run the following commands here, or in the top-level directory to build and install all the utilities:

 - *cmake .*
 - *make*
 - *sudo make install*

**Usage**

1. Copy `eeprom_settings.txt` (or `eeprom_v1_settings.txt` for a V1 HAT) to `myhat_eeprom.txt`.

2. Edit `myhat_eeprom.txt` to suit your specific HAT. 

3. Run `eepmake myhat_eeprom.txt myhat.eep` (or `eepmake -v1 myhat_eeprom.txt myhat.eep` for a V1 HAT) to create the .eep binary.

4. If `eepmake` has generated a product UUID for you, it is a good idea to patch your myhat_eeprom.txt to include and hence preserve it (or use `eepdump` to regenerate it).

**Flashing the EEPROM image**
Follow these steps on the Raspberry Pi after installing the tools and building the .eep file:

1. Disable EEPROM write protection
	* Sometimes this requires a jumper on the board
	* Sometimes this is a GPIO
	* Check your schematics
2. Make sure you can talk to the EEPROM
	* In the HAT specification, the HAT EEPROM is connected to pins that can be driven by I2C0.
	  However, this is the same interface as used by the camera and displays, so use of it by the ARMs is discouraged.
	  The `eepflash.sh` script gets around this problem by instantiating a software driven I2C interface using those
	  pins as GPIOs, calling it `i2c-9`:
	```
	   sudo dtoverlay i2c-gpio i2c_gpio_sda=0 i2c_gpio_scl=1 bus=9
	```
	* Install i2cdetect `sudo apt install i2c-tools`
	* As explained in the HAT+ specification, HAT EEPROMs can have a variety of (hexadecimal) I2C addresses:
	  + 50: standard HAT+ (or legacy HAT)
      + 51: stackable HAT+ (e.g. Raspberry Pi M.2 M Key HAT)
      + 52: stackable Power HAT+ (MODE0)
      + 53: stackable Power HAT+ (MODE1)

      The examples below assume a standard HAT+/HAT, so be prepared to substitute the correct address for another type.

	* Check with `i2cdetect -y 9` (should be at address 0x50)
	```bash
	   i2cdetect -y 9 0x50 0x50
	       0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
	   00: 
	   10:
	   20:
	   30:
	   40:
	   50: 50
	   60: 
	   70:
	```
	Normally, you can skip this step, and assume things are working.
3. Flash eep file `sudo ./eepflash.sh -w -t=24c32 -a=50 -f=eeprom.eep`
4. Enable EEPROM write protection, by undoing step 1 (putting back jumper, or resetting GPIO)

## String data formatting

The tools support two string representations:
1. A single, simple string with no CR/NL, no NUL-termination, and no embedded double-quotes.
2. A multiline string using minimal escaping:
  * String begins with a single `"` followed by CR/NL (carriage return/newline)
  * NULs are escaped as \0, and followed by an extra NL (because they end a string)
  * Backslashes are escaped (`\\`)
  * NLs and TABs are included verbatim
  * CRs are escaped (`\r`)
  * The multiline string is terminated by `\"`
  * Any literal, unescaped CRs found in the string are ignored

`eepmake` attempts to display dt_blob and custom_data blocks as simple strings, falling back to multiline strings, ultimately resorting to hexadecimal data.

If in doubt, put the text in a file, add it using the `-c` option in `eepmake`, and use `eepdump` to see what it looks like. To confirm that blobs data is encoded and preserved correctly, use the `-b <prefix>` option to `eepdump` to save the blob data as separate files.

Examples:
```
dt_blob "rpi-dacpro"

custom_data "
This is the start of a long string.
End this line with a carriage return\r
NUL-terminated\0
\"

custom_data "
NL and NUL-terminated
\0
\"

# This one could have been a simple string
custom_data "
End text with no NL\"

custom_data "
End text with NL
\"
```
