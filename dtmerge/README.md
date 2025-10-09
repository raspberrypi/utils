
# dtmerge, dtoverlay and dtparam

dtmerge is a tool for applying pre-compiled overlays (`*.dtbo` files) to a
base Device Tree file (`*.dtb`).

dtoverlay is a tool for applying pre-compiled overlays to a live system.
dtparam is a tool for applying paramaters of the base Device Tree of a live system.

**Build Instructions**

Install the prerequisites with "sudo apt install cmake libfdt-dev" - you need at least version 3.10 of cmake. Run the following commands here, or in the top-level directory to build and install all the utilities:

 - *cmake .*
    N.B. Use *cmake -DBUILD_SHARED_LIBS=1 .* to build libdtovl as a shared (as opposed to static) library.
 - *make*
 - *sudo make install*

**Usage**

```
Usage:
    dtmerge [<options] <base dtb> <merged dtb> - [param=value] ...
        to apply a parameter to the base dtb, without an overlay (like dtparam)
    dtmerge [<options] <base dtb> <merged dtb> <overlay dtb> [param=value] ...
        to apply an overlay, optionally with parameters (like dtoverlay)
  where <options> is any of:
    -d      Enable debug output
    -h      Show this help message
```
```
Usage:
  dtoverlay <overlay> [<param>=<val>...]
                           Add an overlay (with parameters)
  dtoverlay -D             Dry-run (prepare overlay, but don't apply -
                           save it as dry-run.dtbo)
  dtoverlay -r [<overlay>] Remove an overlay (by name, index or the last)
  dtoverlay -R [<overlay>] Remove from an overlay (by name, index or all)
  dtoverlay -l             List active overlays/params
  dtoverlay -a             List all overlays (marking the active)
  dtoverlay -h             Show this usage message
  dtoverlay -h <overlay>   Display help on an overlay
  dtoverlay -h <overlay> <param>..  Or its parameters
    where <overlay> is the name of an overlay or 'dtparam' for dtparams
Options applicable to most variants:
    -d <dir>        Specify an alternate location for the overlays
                    (defaults to /boot/overlays or /flash/overlays)
    -p <string>     Force a compatible string for the platform
    -v              Verbose operation

Adding or removing overlays and parameters requires root privileges.
```
```
Usage:
  dtparam                Display help on all parameters
  dtparam <param>=<val>...
                         Add an overlay (with parameters)
  dtparam -D             Dry-run (prepare overlay, but don't apply -
                         save it as dry-run.dtbo)
  dtparam -r [<idx>]     Remove an overlay (by index, or the last)
  dtparam -R [<idx>]     Remove from an overlay (by index, or all)
  dtparam -l             List active overlays/dtparams
  dtparam -a             List all overlays/dtparams (marking the active)
  dtparam -h             Show this usage message
  dtparam -h <param>...  Display help on the listed parameters
Options applicable to most variants:
    -d <dir>        Specify an alternate location for the overlays
                    (defaults to /boot/overlays or /flash/overlays)
    -p <string>     Force a compatible string for the platform
    -v              Verbose operation

Adding or removing overlays and parameters requires root privileges.
```
