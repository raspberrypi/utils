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
usage: otamaker [-h] [-C DIR] [-w] [-t | -m | -l] [-c CHUNK_SIZE]
                [-k SIGN_KEY] [-s SIGNATURE] [-f] [--signed-data FILE]
                [--print-pubkey KEY]
                [contents] [outfile]

positional arguments:
  contents              the YAML contents file
  outfile               the artefact name (defaults to the 'name' in the
                        contents)

options:
  -h, --help            show this help message and exit
  -C DIR, --directory DIR
                        the directory payload paths are relative to (default:
                        the current directory)
  -w, --writesum        also write the manifest hash to
                        <outfile>.manifest.sha256sum, and (for a legacy or
                        transitional artefact) the hash of the whole artefact
                        to <outfile>.sha256sum
  -t, --transitional    build a transitional artefact, with a manifest and a
                        legacy contents file for older applicators (the
                        default)
  -m, --manifest        build a manifest-only (format 2) artefact, published
                        by the hash of its manifest, which older applicators
                        can't install
  -l, --legacy          build a legacy artefact, without a manifest
  -c CHUNK_SIZE, --chunk-size CHUNK_SIZE
                        chunk size for large payloads in a manifest artefact
                        (default 4M)
  -k SIGN_KEY, --sign-key SIGN_KEY
                        sign the manifest with this ed25519 private key (PEM)
                        - may be repeated
  -s SIGNATURE, --signature SIGNATURE
                        add the signature line(s) in this file to the manifest
                        signature - may be repeated
  -f, --force           allow an unsupported manifest format, or payload types
                        which need a manifest in a legacy or transitional
                        artefact, e.g. to test applicators
  --signed-data FILE    write the data to be signed to this file, and stop
  --print-pubkey KEY    print the trusted keys line (and key id) for this key,
                        and stop
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

Raspberry Pi Connect Remote Update packages are `zstd`-compressed `tar` archives. The first element of the archive is a YAML file describing the payloads of the update (*). otamaker reads such a YAML file, assembles the other elements into a temporary directory, renaming them as requested, then runs `tar` with the right options.

There are two generations of artefact. A _legacy_ artefact starts with the contents file, `_contents_.yaml`, and is published by the SHA256 of the whole archive, so nothing can be trusted until all of it has been downloaded. A _manifest_ (format 2) artefact starts with `_manifest_.yaml`, which otamaker generates from the contents file by adding the size and SHA256 of every payload (large payloads are hashed in chunks). It is published by the SHA256 of the manifest alone, so the applicator can check each payload as it arrives, and a manifest can be signed. By default otamaker builds a _transitional_ artefact which contains both files and can be installed either way - see "Artefact kinds" below.

Here is an example of an artefact description file:

```
artefact:
  name: AwesomeOS-1.1
  version: 1.1
  description: A vital update recommended to all users
  device_type: rpi5

  payloads:
  - name: preinstall
    type: script
  - name: boot
    type: image-sparse
    source: boot.sparse
  - name: system
    type: image-sparse
    source: system.sparse
  - name: installdata.tgz
    type: tmpfile
  - name: installer
    type: script
```

Any fields which aren't recognised are ignored. However, an error is returned if an update doesn't either write to at least one filesystem or include a script. `payloads` is shown here as a child of `artefact`, which is the preferred layout; a top-level `payloads` is also accepted.

N.B. Although the contents file is structured as a YAML document, only a simple subset of the YAML standard is recognised. It is recommended that you stick to the structure above - anything clever is likely to fail.

The artefact header is meant to provide a high-level view of the contents - what it is, and to which platforms it applies. The values here will appear in the logged output from the Remote Update service, but at present they have no other significance.

(*) If the first element in the archive is not called `_contents_.yaml` or `_manifest_.yaml`, every element is assumed to be a sparse image (see below).

**Payloads**

The following payload types are recognised:
* `image-sparse` - a _sparse image_ of a complete partition, which contains all the data sectors in full but with holes and chunks of fixed values represented efficiently. The name of the payload must match the name of a partition - `boot` and `system` for the `trixie-minbase-ab` images from `rpi-image-gen`. These payloads are written directly to the indicated partition as they are downloaded - this saves time and means you don't need large amounts of temporary storage to host it.
* `eeprom-ab` - a Pi 5 A/B EEPROM update (requires that an AB-compatible EEPROM image is already installed, that the image includes `rpi-connect-ota` 1.3.14 or later and the `rpieepromab` package, and that `config.txt` includes `dtoverlay=nospi10`).
* `script` - a program to execute on the remote device. As the name suggests, this is expected to be a script of some description, but it doesn't have to be.
* `tmpfile` - some other file to be copied to the device for the duration of the update. These are intended to be used by scripts, which they must appear before in the list of payloads in order to be accessible.
* `tar` - a tar archive to be extracted on the device, at the directory given by an optional `path` field (`/` by default), with the ownership and permissions recorded in the archive. The applicator streams it straight through `tar` as each chunk is verified, so it is only available in manifest artefacts (and must be built with `-m` - see below), and it is disabled if the device has `ExecuteScripts=false`.
* `hashlist` - generated by otamaker for payloads which are hashed in chunks; don't write these yourself.

Payload names must be unique, and may not start with `_`, which is reserved for the contents, manifest and signature files.

Each payload is read from the file named by its `source` field, or from the file with the same name as the payload if there is no `source`. Paths are relative to the current directory, unless `-C <dir>` (`--directory`) gives another, as for `tar`. `source` is a build-time field: otamaker removes it from the copy of the contents in the archive. An older form, a comment immediately after (and on the same line as) the `name` field, is still accepted as the path when there is no `source`; such comments are also removed.

**Artefact kinds**

otamaker can build three kinds of artefact, chosen with one of `-t`, `-m` and `-l`:

* `-t` (`--transitional`) - the current default. The archive starts with a legacy `_contents_.yaml` followed by `_manifest_.yaml`, so applicators from before manifests install it as a legacy artefact and newer ones as a manifest artefact. It therefore has two hashes: the SHA256 of the whole archive for older applicators, and the SHA256 of the manifest for newer ones. Give the kind explicitly in scripts which depend on it, because the default will become `-m` at some point.
* `-m` (`--manifest`) - a manifest-only artefact, published by the hash of its manifest. Older applicators can't install it. This is required for `tar` payloads, and for anything else older applicators don't understand.
* `-l` (`--legacy`) - a legacy artefact with just a `_contents_.yaml`, as otamaker has always built.

With `-m` or `-t`, otamaker adds `size` and `sha256` fields to each payload in the manifest. Payloads larger than the chunk size (`-c`, default 4M, up to 64M) are instead hashed one chunk at a time, with the per-chunk digests in an extra `<name>.hashes` payload placed before them, so that the applicator can check a large image as it is written. The manifest gets a `format` field, currently always 2; a `format` in the contents file asks for at least that version, and `-f` (`--force`) allows an unsupported one, e.g. to test applicators. Don't put `size`, `sha256` or `chunk_size` in the contents file yourself.

`-w` (`--writesum`) writes the whole-archive hash to `<outfile>.sha256sum` in legacy and transitional modes, and the manifest hash to `<outfile>.manifest.sha256sum` in manifest and transitional modes.

Note that the manifest hash doesn't change between builds from the same contents and payloads, unlike the whole-archive hash, which depends on timestamps. However, if SOURCE_DATE_EPOCH is set in the environment, the archive members are given that mtime, resulting in consistent whole-archive hashes when run by the same user provided the payloads don't change.

**Signing**

A manifest can be signed, which signs everything it describes. A device with a trusted keys file (`/etc/rpi-connect/rpi-ota-trusted-keys`, one public key per line) then refuses any artefact that isn't a manifest artefact signed by one of its keys, so it can only be updated by its owner, whichever server the artefact comes from. Devices without the file ignore signatures.

Signing uses ed25519 keys, which `openssl` can make:
```
$ openssl genpkey -algorithm ed25519 -out key.pem
$ otamaker --print-pubkey key.pem
```
`--print-pubkey` prints the line to add to the device's trusted keys file, plus the key id. Then build with `-k`:
```
$ otamaker -t -k key.pem contents.yaml
```
`-k` can be repeated to sign with several keys, which is useful while rotating them - one valid signature from a trusted key is enough. The signature goes in a `_manifest_.sig` member just before the manifest; older applicators treat it as a `tmpfile`.

If the private key isn't in a file otamaker can use, e.g. it lives in an HSM, sign in two steps. Run otamaker with `--signed-data` to write the bytes to be signed, sign them to produce a line of the form `ed25519 <key id> <base64 signature>`, then run otamaker again with the same contents and payloads, adding `-s` with the file holding that line:
```
$ otamaker -m --signed-data manifest.tbs contents.yaml
$ SIG=$(openssl pkeyutl -sign -rawin -inkey key.pem -in manifest.tbs | base64 -w 0)   # or your HSM's equivalent
$ echo "ed25519 <key id> $SIG" > manifest.sig
$ otamaker -m -s manifest.sig contents.yaml
```
where `<key id>` is as printed by `--print-pubkey`.

Signed artefacts can also carry a `security_version` field in the artefact header. Once a device has installed and committed an artefact with a given `security_version`, it refuses anything with a lower one, which stops an old signed artefact with a known problem being installed again. Only raise it for releases that fix security problems.

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
Put that in a file called `blinky`, then create `blinky.yaml`:
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
  _manifest_.yaml
  blinky

Artefact: blinky.tar.zst
SHA256:   9fbb02297d0229893b61a44559737e462c94ceeb109e263cca45704caf0064c2 (whole artefact)
SHA256:   637c1526b9df4200f960fdf31f14f3496e5205b0a0dc9a651c508fcc5fca1a58 (manifest)
```
See how the YAML file, even though it was called `blinky.yaml`, ended up being called `_contents_.yaml` as required, and that otamaker has added a `_manifest_.yaml` because it built a transitional artefact. There are two hashes: use the manifest hash for devices whose `rpi-connect-ota` understands manifests, and the whole-artefact hash for older ones. The whole-artefact hash is different every time because of the changing timestamps in the temporary directory that `otamaker` creates, but the manifest hash only changes when the contents or the payloads do.

Artefacts can be hosted anywhere you like as long as it is accessible to the target. Find your host computer's IP address on your local network using `hostname -I` - on a home network it is likely to be of the form `192.168.x.y`, where `x` and `y` are numbers. Now run up an HTTP server on your host. This wouldn’t normally be the target device to update, but it will work if you only have one Pi.
```
$ python3 -m http.server 8000 --directory .
```
When you create the artefact as part of adding a new deployment, the URI you will need to provide will be something like `http://192.168.x.y:8000/blinky.tar.zst`, substituting your own values for `x` and `y`; the SHA256 checksum should be copied exactly from the output of `otamaker` (the manifest hash, unless the device is too old to understand manifests).

Before clicking the "Create and deploy" button (just "Deploy" if you have alread created the artefact), locate the green LED on your target Pi. Watch it as you click - it should flash for a second or two.

**Example 2: An A/B update artefact**

Now let's create an artefact suitable to update A/B image. This will only work if you've used a tool like `rpi-image-gen` to build an image with the necessary partitions.

```
artefact:
  name: abupdate
  version: 1.0.3
  device_type: rpi

  payloads:
  - name: boot
    type: image-sparse
    source: work/image-myapp-1.0.3/boot.sparse
  - name: system
    type: image-sparse
    source: work/image-myapp-1.0.3/system.sparse
```
This artefact YAML file is designed to be used from root of the rpi-image-gen repository - you could also drop the paths, keeping `boot.sparse` and `system.sparse`, and either run `otamaker` from within the `work/image-myapp-1.0.3` directory (or your equivalent) or point it there with `-C`. Here's the output it generated:
```
pi@philpi5:~/rpi-image-gen $ otamaker abupdate.yaml
Contents:
  _contents_.yaml
  _manifest_.yaml
  boot.hashes
  boot
  system.hashes
  system

Artefact: abupdate.tar.zst
SHA256:   fa156308d8baeee47c7c610142a4d509a80b9c3b96ea21f0bbbb7740583eb8ba (whole artefact)
SHA256:   54fffd50f0b73544e321315541158e2ea202ad82337828ba2f0f21fa80be8a9a (manifest)
pi@philpi5:~/rpi-image-gen $ ls -l abupdate.tar.zst
-rw-rw-r-- 1 pi pi 248315371 Nov  3 12:44 abupdate.tar.zst
```
The images are larger than the 4M chunk size, so each gets a `.hashes` payload ahead of it holding the SHA256 of every chunk, and the applicator checks each chunk before writing it to the partition. This is significantly larger than blinky, and may take a minute or more to install. At the end, the device will reboot into the new `boot` and `system` partitions, hopefully marking the deployment as successfully completed when it reconnects.
