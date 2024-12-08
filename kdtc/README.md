# kdtc

`kdtc` is a wrapper around `dtc` (the Device Tree compiler) that passes input files through `cpp` (the C preprocessor) first, in order to resolve any #include directives or expand any #define macros. When run without any `dtc` options, `kdtc` attempts to detect the input format and do the right thing.

**Build Instructions**

Install the prerequisites with "sudo apt install cmake device-tree-compiler" - you need at least version 3.10 of cmake. Run the following commands, either here or in the top-level directory to build and install everything:

 - *cmake .*
 - *sudo make install*

Alternatively, to avoid fetching the entire `utils` repository and its history, you can just download it with:
```
$ wget https://raw.githubusercontent.com/raspberrypi/utils/refs/heads/master/kdtc/kdtc
$ chmod +x kdtc
```

**Usage**
```
Usage: kdtc [<opts>] [<infile> [<outfile>]]
  where <opts> can be any of:

    -h|--help            Show this help message
    -i|--include <path>  Add a path to search for include files
    -k|--kerndir <path>  The path to the kernel tree
    -n|--just-print      Just show the command that would be executed
    -w|--warnings        Don't suppress common dtc warnings

  or any dtc options (see 'dtc -h')

  When run with no dtc options, kdtc detects the input format and attempts
  to do the right thing.
```

Assuming `kdtc` is in your PATH, compiling an overlay without `#include`s is simple:
```
$ kdtc widget-overlay.dts widget.dtbo
```
For an overlay written as if it were in the kernel tree with `#include`s, use either:
```
$ kdtc -k /path/to/kernel doofer-overlay.dts doofer.dtbo
```
Or, running from a directory within the kernel tree:
```
$ kdtc /path/to/whatsit-overlay.dts /path/to/whatsit.dtbo
```
(When run within a git kernel source tree, the `--kerndir` path is inferred.)

To decompile an overlay to `stdout`:
```
$ kdtc mystery.dtbo
```
