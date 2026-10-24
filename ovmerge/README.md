
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
usage: ovmerge [-h] [-b branch] [-B token] [-c] [-e] [-f] [-i] [-l] [-n] [-N] [-p] [-q] [-r] [-s] [-S n] [-t] [-T] [-w] [ovspec ...]

Merge DT overlay source files, flatten and sort .dts files, or display the include tree.

positional arguments:
  ovspec      The name of an overlay, optionally followed by a comma-separated
              list of parameters, each with optional '=<value>' assignments.
              The presence of any parameters, or a comma followed by no
              parameters, removes the parameter declarations from the merged
              overlay to avoid a potential name clash.

options:
  -h, --help  show this help message and exit
  -b branch   Read files from specified git branch
  -B token    Set a debug breakpoint on the specified token number
  -c          Include 'redo' comment with command line (c.f. '-r')
  -e          Expand mode - list non-skipped lines in order of inclusion
  -f          Force some errors to be ignored
  -i          Show include hierarchy for each file
  -l          Like expand mode, but labels each line with source file
  -n          No .dts file header (just parsing .dtsi files)
  -N          Don't renumber overlay fragments (not guaranteed to work)
  -p          Emulate Pi firmware manipulation
  -q          Query mode (no output, just the success/failure return code)
  -r          Redo command comment in named files (c.f. '-c')
  -s          Sort nodes and properties (for easy comparison)
  -S n        Instead of tabs, use 'n' spaces for indentation
  -t          Trace the tree changes
  -T          Trace the parsing process
  -w          Show warnings
```
