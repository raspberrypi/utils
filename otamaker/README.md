# otamaker

otamaker is a tool for building Remote Update/Over-The-Air packages for Raspberry Pi Connect Remote Update.

**Build Instructions**

Install the prerequisites with "sudo apt install cmake python3" - you need at least version 3.10 of cmake. Run the following commands, either here or in the top-level directory to build and install everything:

 - *cmake .*
 - *make*
 - *sudo make install*

Or just download and run it - it's a Python3 script.

**Usage**

```
usage: otamaker [-h] [-w] contents [outfile]

positional arguments:
  contents        the YAML contents file
  outfile         the artefact name (defaults to the 'name' in the contents)

options:
  -h, --help      show this help message and exit
  -w, --writesum  also write the sha256sum to <outfile>.sha256sum
```

**Enabling Rasberry Pi Connect Remote Updates**

In order to use the Remote Update facility, you first need to enable it. The following commands 
should install the necessary software, if it isn't already present:
```
$ sudo apt update
$ sudo apt install rpi-connect rpi-connect-ota
```
or, if you are running on Raspberry Pi OS Lite:
```
$ sudo apt update
$ sudo apt install rpi-connect-lite rpi-connect-ota
```
Remote Update is an experimental that isn't yet enabled by default. To do so, run `rpi-connnect` with the new `ota` parameter:
```
$ rpi-connect ota on
```
You will be prompted to enter your password to grant it the necessary permissions.

**Update structure**

Raspberry Pi Connect Remote Update packages are `zstd`-compressed `tar` archives. The first element of the archive should be a YAML file describing the payloads of the update (*). otamaker reads such a YAML file, assembles the other elements into a temporary directory, renaming them as requested, then runs `tar` with the right options.

Here is an example of an artefact description file:

```
artefact:
  name: AwesomeOS-1.1
  version: 1.1
  description: A vital update recommended to all users
  device_type: rpi5

payloads:
- name: boot          # boot.sparse
  type: image-sparse
- name: system        # system.sparse
  type: image-sparse
- name: installdata.tgz
  type: tmpfile
- name: installer
  type: script
```

Any fields and payload types which aren't recognised are ignored. However, an error is returned if an update doesn't either write to at least one filesystem or include a script.

N.B. Although the contents file is structured as a YAML document, only a simple subset of the YAML standard is recognised. It is recommended that you stick to the structure above - anything clever is likely to fail.

The artefact header is meant to provide a high-level view of the contents - what it is, and to which platforms it applies. The values here will appear in the logged output from the Remote Update service, but at present they have no other significance.

(*) If the first element in the archive is not called `_contents_.yaml`, every element is assumed to be a sparse image (see below).

**Payloads**

The following payload types are recognised:
* `image-sparse` - a _sparse image_ of a complete partition, which contains all the data sectors in full but with holes and chunks of fixed values represented efficiently. The name of the payload must match the name of a partition - `boot` and `system` for the `trixie-minbase-ab` images from `rpi-image-gen`. These payloads are written directly to the indicated partition as they are downloaded - this saves time and means you don't need large amounts of temporary storage to host it.
* `script` - a program to execute on the remote device. As the name suggests, this is expected to be a script of some description, but it doesn't have to be.
* `tmpfile` - some other file to be copied to the device for the duration of the update. These are intended to be used by scripts, which they must appear before in the list of payloads in order to be accessible.

Comments immediately after (and on the same line as) the `name` field are interpreted by otamaker as a path to the source file. These comments are removed when the final archive is created.

**Scripts**

It is expected that executable payloads will be written in one of the many scripting languages - Bourne shell, Perl, Python 3, etc. - because they are largely independent of the device architecture, but it could be any program that can be run on the target device. For interpreted languages this requires that the interpreter and any associated library modules are already installed on the target. It is also necessary to use a standard "shebang" (`#!/path/to/...`) header, so the target knows how to run it. Either way, executable payloads are referred to as scripts from now on.

Script and tmpfile payloads are downloaded into a temporary directory, and deleted afterwards along with any other files generated there. Scripts are run with no parameters, but their working directory is set to be the temporary directory. In this way they can refer to other files without paths.

The simplest script that uses a tmpfile would be something like:

```
#!/bin/sh
cat message
exit 0
```

where `message` is another, earlier payload, which may or may not hold the first verse of _Never Gonna Give You Up_. This is not especially useful unless you like filling your log files with Rick Astley lyrics, but if you make it a tar archive then it creates an easy way to patch an existing image.

The return code from a script is important. The default return value (0) indicates that the script was successful, 1 means that it failed, and 2 means that it was successful and would like the system to reboot after the update is applied.

N.B. Update scripts are run as root, in order to give them sufficient privilege to modify filesystems, etc. For this reason, be careful what you put in your scripts (and where you source and host your artefacts).

**Example 1: Blinky**

`Blinky` is an artefact that flashes a Pi's LED; it could be useful if you have multiple devices and you want to be sure which is which. The script is fairly simple, only made more complex because of the desire to return the LED to the state it was previously in. We're going to cheat and use Bash because it can count.

```
#!/bin/bash
LED=/sys/class/leds/ACT/trigger
OLD_TRIGGER=$(cut -d'[' -f2 $LED | cut -d']' -f1)
for i in {1..8}; do
    echo default-on > $LED
    sleep 0.1
    echo none > $LED
    sleep 0.1
done
echo $OLD_TRIGGER > $LED
```
Put that in a file called `blinky`, then create `blink.yaml`:
```
artefact:
  name: blinky
  version: 1.0
  device_type: rpi

payloads:
- name: blinky
  type: script
```
Now we can create the artefact:
```
$ otamaker blinky.yaml
Contents:
  _contents_.yaml
  blinky

Artefact: blinky.tar.zst
SHA256:   1beab01defe22e7831fffd2ee42342bf85a50da2673628bbf742f6170b13c445
```
See how the YAML file, even though it was called `blink.yaml`, ended up being called `_contents_.yaml` as required. You will get a different SHA256 every time because of the changing timestamps in the temporary directory that `otamaker` creates.

Artefacts can be hosted anywhere you like as long as it is accessible to the target. Find your host computer's IP address on your local network using `hostname -I` - on a home network it is likely to be of the form `192.168.x.y`, where `x` and `y` are numbers. Now run up an HTTP server on your host. This wouldn’t normally be the target device to update, but it will work if you only have one Pi.
```
$ python3 -m http.server 8000 --directory .
```
When you create the artefact as part of adding a new deployment, the URI you will need to provide will be something like `http://192.168.x.y:8000/blink.tar.zst`, substituting your own values for `x` and `y`; the SHA256 checksum should be copied exactly from the output of `otamaker`.

Before clicking the "Create and deploy" button (just "Deploy" if you have alread created the artefact), locate the green LED on your target Pi. Watch it as you click - it should flash for a second or two.

**Example 2: An A/B update artefact**

Now let's create an artefact suitable to update A/B image. This will only work if you've used a tool like `rpi-image-gen` to build an image with the necessary partitions.

```
artefact:
  name: abupdate
  version: 1.0.3
  device_type: rpi

payloads:
- name: boot          # work/image-myapp-1.0.3/boot.sparse
  type: image-sparse
- name: system        # work/image-myapp-1.0.3/system.sparse
  type: image-sparse
```
This artefact YAML file is designed to be used from root of the rpi-image-gen repository - you could also drop the paths, keeping `boot.sparse` and `system.sparse`, and run `otamaker` from within the `work/image-myapp-1.0.3` directory (or your equivalent). Here's the output it generated:
```
pi@philpi5:~/rpi-image-gen $ otamaker abupdate.yaml 
Contents:
  _contents_.yaml
  boot
  system

Artefact: abupdate.tar.zst
SHA256:   fa156308d8baeee47c7c610142a4d509a80b9c3b96ea21f0bbbb7740583eb8ba
pi@philpi5:~/rpi-image-gen $ ls -l abupdate.tar.zst
-rw-rw-r-- 1 pi pi 248291841 Mar 13 16:19 abupdate.tar.zst
```
This is significantly larger than blinky, and may take a minute or more to install. At the end, the device will reboot into the new `boot` and `system` partitions, hopefully marking the deployment as successfully completed when it reconnects.
