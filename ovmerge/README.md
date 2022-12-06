
# ovmerge

ovmerge is tool for merging DT overlay source files (`*-overlay.dts`),
flattening and sorting `.dts` files for easy comparison, displaying
the include tree, etc.

**Build Instructions**

Install the prerequisites with "sudo apt install cmake" - you need at least version 3.10 of cmake. Run the following commands here, or in the top-level directory to build and install all the utilities:

 - *cmake .*
 - *make*
 - *sudo make install*

**Usage**

```
Usage: ovmerge <options> <ovspec>
  where <ovspec> is the name of an overlay, optionally followed by
    a comma-separated list of parameters, each with optional '=<value>'
    assignments. The presence of any parameters, or a comma followed by
    no parameters, removes the parameter declarations from the merged
    overlay to avoid a potential name clash.
  and <options> are any of:
    -b <branch>  Read files from specified git branch
    -c      Include 'redo' comment with command line (c.f. '-r')
    -e      Expand mode - list non-skipped lines in order of inclusion
    -h      Display this help info
    -i      Show include hierarchy for each file
    -l      Like expand mode, but labels each line with source file
    -n      No .dts file header (just parsing .dtsi files)
    -p      Emulate Pi firmware manipulation
    -r      Redo command comment in named files (c.f. '-c')
    -s      Sort nodes and properties (for easy comparison)
    -t      Trace
    -w      Show warnings
```
