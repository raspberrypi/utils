# rpi-eeprom-ab

The Raspberry Pi EEPROM AB service is a mailbox-based API
that allows you to update and manage the AB EEPROM partitions.

Although this service can be used via raw vcmailbox commands the
recommended API is the command line `rpi-eeprom-ab` application.

This service currently only exists on the Raspberry Pi 5 family of
devices running AB-capable firmware.

## Build Instructions

```
mkdir build
cd build
cmake ..
make
sudo make install
```

If overwriting the system (APT) installed `rpi-eeprom-ab`, set the CMake
install prefix to `/usr`. Otherwise, there will be a library mismatch because
the default install prefix is `/usr/local`:

```
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
```

## Usage

Display usage instructions for all operations:

```
rpi-eeprom-ab help
```

Show the application and library version:

```
rpi-eeprom-ab version
```

### EEPROM read and write

Update the opposite AB partition with a new bootloader image. The update file
must be a valid partition image and must meet the board's minimum bootloader
version. An update can only be performed when the current partition
is committed.

```
rpi-eeprom-ab update pieeprom-ab.bin
```

Extract a partition image from an AB-capable full pieeprom image:

```
dd if=pieeprom.bin bs=1K skip=64 count=988 of=pieeprom-ab.bin
```

Read the currently selected AB partition to a file:

```
rpi-eeprom-ab read current-partition.bin
```

Read the entire 2 MiB EEPROM to a file:

```
rpi-eeprom-ab dump eeprom-full.bin
```

Get the current status of an EEPROM update:

```
rpi-eeprom-ab update-status
```

### Partition info

Get the currently selected AB partition (`A` or `B`):

```
rpi-eeprom-ab partition
```

Get whether the current partition is committed (`0` or `1`):

```
rpi-eeprom-ab committed
```

Get the committed and valid partition selections and their SHA-256 hashes:

```
rpi-eeprom-ab partition-status
```

Get the partition used at boot and whether it was committed at time of boot:

```
rpi-eeprom-ab status-at-boot
```

### Partition validation and commit

Mark the uncommitted partition as valid.
The hash (a 64-character hex string) must match the SHA-256 hash of the update image.

```
rpi-eeprom-ab mark-partition-valid <hash>
```

Revert to the committed partition as the valid selection, overwriting a
previous `mark-partition-valid`. The hash must match the SHA-256 hash of the
committed partition:

```
rpi-eeprom-ab revert-to-committed <hash>
```

Commit the current AB partition:

```
rpi-eeprom-ab commit
```

Force commit the opposite partition. When this is used, the partition will not
automatically be rolled back if there is a failure.

```
rpi-eeprom-ab force-commit-opposite
```

### Tryboot

When tryboot is enabled (`1`), the bootloader will attempt to boot from the
valid but uncommitted partition on the next reboot. When disabled (`0`), only
the committed partition is used.

Get the current tryboot value:

```
rpi-eeprom-ab tryboot
```

Enable or disable tryboot:

```
rpi-eeprom-ab tryboot 1
rpi-eeprom-ab tryboot 0
```

### Typical update workflow

1. Write the new image to the opposite partition:
   ```
   rpi-eeprom-ab update pieeprom-ab.bin
   ```
2. Mark the updated partition as valid. The hash must match the SHA-256 hash of
   the image written in step 1:
   ```
   export UPDATE_HASH="$(sha256sum pieeprom-ab.bin | awk '{print $1}')"
   rpi-eeprom-ab mark-partition-valid "$UPDATE_HASH"
   ```
3. Enable tryboot and reboot. The tryboot flag is a one-shot flag that is
   cleared during boot, so a failed boot falls back to the committed
   partition next time:
   ```
   rpi-eeprom-ab tryboot 1
   sudo reboot
   ```
4. After rebooting, verify that the system booted from the new (uncommitted)
   partition. `committed` returns `0` when running from the uncommitted
   partition, confirming tryboot succeeded:
   ```
   rpi-eeprom-ab committed
   ```
5. Commit the partition so it is used for all future boots:
   ```
   rpi-eeprom-ab commit
   ```

#### Forced update alternative

Steps 1 and 2 are the same as above. Then, instead of using tryboot, force
commit the newly written (opposite) partition so it becomes the partition used
for all future boots. This skips the tryboot rollback mechanism, so only use it
when the new image is known to be good:

```
rpi-eeprom-ab force-commit-opposite
```

## Error handling and debug

If the firmware reports an error then `rpi-eeprom-ab` prints a descriptive
message and returns a non-zero exit code.

The firmware logs can be viewed with `sudo vclog -m` for additional debug.
