# rpi-ram-authenticity-check

Read-only RAM authenticity check for BCM2712 devices (the Pi 5 family).
Compares the RAM capacity in the running board revision (from device tree or
`/proc/cpuinfo`) with the factory revision in OTP row 32.

Run `rpi-ram-authenticity-check` after installing utils, or directly from this
repository:

```sh
./rpi-ram-authenticity-check/rpi-ram-authenticity-check
```

Requires `vcgencmd` and access to `/dev/vcio`; run with `sudo` if access is denied.
Use `--help` for usage information.

A mismatch warns of possible RAM tampering or replacement and advises contacting
the seller to arrange a return. Matching capacity is reported as "RAM appears
authentic", but cannot detect a replacement with the same capacity.

Exit status:

* `0`: capacity matches.
* `1`: capacity differs.
* `2`: unable to check, including unsupported hardware or missing/invalid data.

When installed, the checker is also called by `raspinfo` on BCM2712 devices.
Its output, including warnings, is included in `raspinfo.txt`.
