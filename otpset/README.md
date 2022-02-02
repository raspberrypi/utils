# otpset
Wrapper script for programming the OTP on Raspberry Pi devices.

OTP setting is done via `vcmailbox` calls, and are not particularly easy to use. This wrapper makes it easier to read and set customer OTP bits, and provides some level of error checking to the process.

Once OTP bits are set, they CANNOT be unset, so this wrapper requests confirmation before performing destructive/irreversible operations. 

See official docs on the subject here: https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#industrial-use-of-the-raspberry-pi
