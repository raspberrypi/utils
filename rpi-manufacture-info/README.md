# rpi-manufacture-info

Displays the manufacturing information provisioned in the OTP of BCM2711
(Raspberry Pi 4 family) and BCM2712 (Raspberry Pi 5 family) devices during
production.

| Field                | BCM2711         | BCM2712                   |
|----------------------|-----------------|---------------------------|
| Serial number        | row 28          | rows 35 and 31            |
| Board revision       | row 30          | row 32                    |
| QrCode               | -               | `/chosen/rpi-duid` (from rows 109-114) |
| MAC addresses        | rows 64-65      | rows 50-55 (Ethernet, Wi-Fi, Bluetooth) |
| Board attributes     | row 33          | row 33                    |
| SDRAM_CONFIG         | row 33 bits 16:8 | row 34 bits 11:0, override in bits 27:16 |

SDRAM_CONFIG is decoded to the total SDRAM size and the number of ranks. The
size is converted to the equivalent board revision memory size field and
compared with the memory size in the OTP board revision and the running board
revision. Sizes which are not a power of two (i.e. 3GB) are encoded as
"Other" in the board revision. Older devices were manufactured without
SDRAM_CONFIG, in which case the memory size is taken from the board revision.

On Compute Modules (CM4, CM4S, CM5 and CM5 Lite) the board attributes are
decoded to show whether eMMC and Wi-Fi are fitted. These bits were not
programmed on older devices, so a CM5 Lite is also identified from the board
type in the board revision.

Run `rpi-manufacture-info` after installing utils, or directly from this
repository:

```sh
./rpi-manufacture-info/rpi-manufacture-info
```

Requires `vcgencmd` and access to `/dev/vcio`; run with `sudo` if access is
denied.

When installed, `raspinfo` includes the output on BCM2711 and BCM2712 devices.
