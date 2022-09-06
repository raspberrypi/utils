
# vclog

Tool to fetch VideoCore logs ( assert or msg ) and display them on
the terminal.
The addresses where logs can be found are fetched from the
device tree and then using headers found inside that region of memory,
the code accesses the log type of interest and parses it.

**Build Instructions**

Install cmake with "sudo apt install cmake" - you need at least version 3.10.
After cloning this repo 'git clone https://github.com/raspberrypi/utils'
it is advised to create a binary folder seperate from the source ie 'build'
for where CMake can generate the build pipeline:

 - *mkdir build*
 - *cd build*
 - *cmake ..*
 - *make*

**Usage**

* sudo ./vclog [-f] <-m|-a>
* sudo ./vclog [--follow] <--msg|--assert>

**Notes**

* Structs describing how VC arranges it's data within the regions of
	interest must keep the same size values.
	Since the VC might have a different memory model from the host (32-bit
	vs 64-bit) structs values need to be of the same size.

* memcpy load pair on aarch64 requires the src address to be aligned with
	8 bit boundaries but also at (src + size of memory to copy).
	memcpy was used so that we could capture the current state of the logs
	for parsing, as logs are written to a circular buffer.

* Logs are written to a circular buffer - so VideoCore keeps track of:

  A) Oldest message (first message user wants to read) which gets wiped if
	circular buffer wraps around back to start

  B) Next region in circular buffer where VideoCore will write its next
	message
