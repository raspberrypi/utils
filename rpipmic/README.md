# rpipmic

Tool to report PMIC events on Raspberry Pi 5 family devices (Pi 5, Pi 500,
CM5).

The bootloader clears fatal PMIC events at power on, so the reason for the
last PMIC reset is read from /proc/device-tree/chosen/power/power_reset.
Events latched since boot are read from PMIC register 0x3018 using vcgencmd,
if available.
