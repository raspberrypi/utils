
# overlaycheck

overlaycheck is a tool for validating the overlay files and README in a
kernel source tree. Note that overlaycheck makes use of ovmerge, dtmerge, dtc,
dtdiff and fdtget, and therefore requires them all to be installed and on the
path.

**Build Instructions**

Install the prerequisites with "sudo apt install cmake device-tree-compiler" - you need at least version 3.10 of cmake. Run the following commands, either here or in the top-level directory to build and install everything:

 - *cmake .*
 - *make*
 - *sudo make install*

**Usage**

* overlaycheck
* overlaycheck -v     (for verbose output)
* overlaycheck -s     (to see all the strict warnings from dtc)
