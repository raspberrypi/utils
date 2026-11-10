# raspinfo
Short script to dump information about the Pi. Use it for annotating bug reports.

On BCM2712 devices, includes the output of
[`rpi-ram-authenticity-check`](../rpi-ram-authenticity-check/) when installed.
RAM mismatch warnings and unavailable checks are recorded in `raspinfo.txt`
without stopping the rest of the report.
