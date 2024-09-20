# utils
A collection of scripts and simple applications

* [dtmerge](dtmerge/) - A tool for applying compiled DT overlays (`*.dtbo`) to base Device
    Tree files (`*.dtb`). Also includes the `dtoverlay` and `dtparam` utilities.
* [eeptools](eeptools/) - Tools for creating and managing EEPROMs for HAT+ and HAT board.
* [kdtc](kdtc/) - A tool for compiling overlays with #includes, etc., as used in the kernel tree.
* [otpset](otpset/) - A short script to help with reading and setting the customer OTP
    bits.
* [overlaycheck](overlaycheck/) - A tool for validating the overlay files and README in a
    kernel source tree.
* [ovmerge](ovmerge/) - A tool for merging DT overlay source files (`*-overlay.dts`),
    flattening and sorting `.dts` files for easy comparison, displaying
    the include tree, etc.
* [pinctrl](pinctrl/) - A more powerful replacement for raspi-gpio, a tool for
    displaying and modifying the GPIO and pin muxing state of a system, bypassing
    the kernel.
* [raspinfo](raspinfo/) - A short script to dump information about the Pi. Intended for
    the submission of bug reports.
* [vclog](vclog/) - A tool to get VideoCore 'assert' or 'msg' logs
    with optional -f to wait for new logs to arrive.


**Build Instructions**

Install the prerequisites with "sudo apt install cmake device-tree-compiler libfdt-dev" - you need at least version 3.10 of cmake. Run the following commands to build and install everything, or see the README files in the subdirectories to just build utilities individually:

 - *cmake .*
 - *make*
 - *sudo make install*
