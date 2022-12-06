
# dtmerge

dtmerge is a tool for applying pre-compiled overlays (`*.dtbo` files) to a
base Device Tree file (`*.dtb`).

**Build Instructions**

Install the prerequisites with "sudo apt install cmake libfdt-dev" - you need at least version 3.10 of cmake. Run the following commands here, or in the top-level directory to build and install all the utilities:

 - *cmake .*
 - *make*
 - *sudo make install*

**Usage**

```
Usage:
    dtmerge [<options] <base dtb> <merged dtb> - [param=value] ...
        to apply a parameter to the base dtb (like dtparam)
    dtmerge [<options] <base dtb> <merged dtb> <overlay dtb> [param=value] ...
        to apply an overlay with parameters (like dtoverlay)
  where <options> is any of:
    -d      Enable debug output
    -h      Show this help message
```
