
# rpifwcrypto

The Raspberry Pi Firmware Cryptography service is a mailbox based API
that allows a limited set of cryptographic operations to be performed
by the firmware without exposing private keys to userspace.

The initial implementation is designed to support PiConnect and
provides an ECDSA P-256 SHA256 signature API.

A SHA256 HMAC API is provided to provide basic support for derived keys
instead of using the raw device unique private key
e.g. HMAC(serial-number + EMMC CID) could be used for a LUKS passphrase.

Although this service can be used via raw vcmailbox commands the
recommended API is either the command line rpi-fw-crypto application
or the librpifwcrypto.so shared library.

**Build Instructions**
Install prerequisites with "sudo apt install cmake libgnutls28-dev"" - you need at least version 3.10.

 - *mkdir build*
 - *cd build*
 - *cmake ..*
 - *make*
 - *sudo make install*

**Usage**

* rpi-fw-crypto -h                                                              (Displays usage instructions for all operations)
* rpi-fw-crypto get-num-otp-keys                                                (Returns the number of OTP key slots)
* rpi-fw-crypto sign --in message.bin --key-id 1 --alg ec --out sig.bin         (Signs message.bin with the device unique OTP key (id 1))
* rpi-fw-crypto get-key-status 1                                                (Gets the status of key-id 1)
* rpi-fw-crypto set-key-status 1 LOCKED                                         (Blocks the raw OTP read API on this key until the device is rebooted)
* rpi-fw-crypto hmac --in message.bin --key-id 1 --out hmac.bin                 (Generates the SHA256 HMAC of message.bin and OTP key id 1)

** Notes **
The device unique private key can be provisioned with the `rpi-otp-private-key` utility.
This MUST be a raw ECDSA P-256 key and not just a random number.

This service is not a hardware security module and the current implementation
does not protect the key and/or OTP from being accessed directly with root level privileges.
It just removes the need to expose the key to userspace (e.g. initramfs) scripts.
