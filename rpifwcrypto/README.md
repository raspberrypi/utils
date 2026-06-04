
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
* rpi-fw-crypto get-key-usage 1                                                 (Gets the usage of key-id 1)
* rpi-fw-crypto set-key-usage 1 0x1                                             (Sets the usage of key-id 1 to RPI_CONNECT in OTP)
* rpi-fw-crypto hmac --in message.bin --key-id 1 --out hmac.bin                 (Generates the SHA256 HMAC of message.bin and OTP key id 1)
* rpi-fw-crypto pubkey --key-id 1 --out device-pub.der                          (Derives and retrieves the corresponding public key for the specified device private ECDSA P256 key)
* rpi-fw-crypto privkey --key-id 1 --out device-priv.der                        (Retrieves the device private key - in DER form - will error if key status is locked)
* rpi-fw-crypto genkey --key-id 1 --alg ec                                      (Generates an ECDSA P256 key-pair and writes the private key to the OTP)

**Locking the device private key**

Access to the device private key can be locked by default at boot by setting
`lock_device_private_key=1` in config.txt. This blocks the raw OTP read API
(`rpi-fw-crypto privkey`) for the key until the device is rebooted, whilst
still allowing the sign, hmac and pubkey operations.

The contents of config.txt (within boot.img) is authenticated by the firmware
if secure-boot is enabled and `lock_device_private_key=1` should always be
specified if secure-boot is enabled.

**OpenSSL equivalents**

The firmware uses MbedTLS to implement the cryptographic operations. For
reference / test, here are the OpenSSL equivalents. In these examples
`private_key.pem` is a local copy of the device private key. If the key
status is not LOCKED it can be extracted and converted to PEM with:

```
rpi-fw-crypto privkey --key-id 1 --out device-priv.der
openssl ec -inform DER -in device-priv.der -outform PEM -out private_key.pem
```

```
# sign - SHA256 hash of the input, ECDSA P-256 signature in DER form
openssl pkeyutl -sign -inkey private_key.pem -rawin -in message.bin -out sig.bin
openssl pkeyutl -verify -pubin -inkey device-pub.der -sigfile sig.bin -rawin -in message.bin

# hmac - HMAC-SHA256 keyed with the raw 32-byte OTP key value
openssl dgst -sha256 -mac HMAC -macopt hexkey:"$(rpi-otp-private-key)" message.bin

# pubkey - DER (SubjectPublicKeyInfo) public key derived from the private key
openssl ec -in private_key.pem -pubout -outform DER -out device-pub.der

# genkey - ECDSA P-256 (prime256v1) key-pair generation
openssl ecparam -name prime256v1 -genkey -noout -out private_key.pem
```

**Error handling and debug**

If the firmware reports an error then `rpi-fw-crypto` prints the error
e.g. `Last crypto error: 4 (Key locked)` and sets the exit code to the
negated firmware error code (`RPI_FW_CRYPTO_STATUS` in `rpifwcrypto.h`).
Since shells report exit codes as an unsigned byte this appears as
`256 - N` e.g. `KEY_LOCKED` (4) gives an exit code of 252.

The firmware logs can be viewed with `sudo vclog -m` for additional debug.

** Notes **
The device unique private key can also be provisioned with the `rpi-otp-private-key` utility.
This MUST be a raw ECDSA P-256 key and not just a random number.

This service is not a hardware security module and the current implementation
does not protect the key and/or OTP from being accessed directly with root level privileges.
It just removes the need to expose the key to userspace (e.g. initramfs) scripts.
