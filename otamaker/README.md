# otamaker and Remote Update artefacts

`otamaker` is a tool for building artefacts: the update packages used by
Raspberry Pi Connect Remote Update. This document describes how to build them,
what they can contain, and how a device decides whether to accept them.

On the device, updates are applied by the `rpi-connect-ota` package. Some
features need a recent version of it; where that matters, the version is given.

**Contents**

- [Installing otamaker](#installing-otamaker)
- [Enabling Remote Update](#enabling-remote-update)
- [Artefact structure](#artefact-structure)
- [Payloads](#payloads)
- [Artefact kinds and hashes](#artefact-kinds-and-hashes)
- [Signing](#signing)
- [Security versions](#security-versions)
- [Device settings](#device-settings)
- [Scripts](#scripts)
- [Example 1: Blinky](#example-1-blinky)
- [Example 2: An A/B update](#example-2-an-ab-update)

## Installing otamaker

otamaker is a single Python 3 script, so you can just download and run it. It
is also installed by the `rpi-connect-ota` package, and can be built and
installed from the `raspberrypi/utils` repository: install the prerequisites
with `sudo apt install cmake python3` (you need at least version 3.10 of
cmake), then run the following commands, either in the `otamaker` directory or
in the top-level directory to build and install everything:

```
cmake .
make
sudo make install
```

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

## Enabling Remote Update

To use Remote Update, you first need to enable it on the device. The following
commands install the necessary software, if it isn't already present:

```
$ sudo apt update
$ sudo apt install rpi-connect rpi-connect-ota
```

or, if you are running Raspberry Pi OS Lite:

```
$ sudo apt update
$ sudo apt install rpi-connect-lite rpi-connect-ota
```

Remote Update is an experimental feature that isn't yet enabled by default. To
enable it, run `rpi-connect` with the `ota` parameter:

```
$ rpi-connect ota on
```

You will be prompted for your password, to grant it the necessary permissions.

## Artefact structure

An artefact is a `zstd`-compressed `tar` archive. The first member describes
the payloads which follow. otamaker reads a YAML _contents file_ describing
the artefact, gathers the payloads, adds what the device needs to check them,
and creates the archive.

Here is an example of a contents file:

```yaml
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

The artefact header gives a high-level view of the contents: what it is, and
which devices it is for. These values appear in the logs of the Remote Update
service, but at present have no other effect. The exceptions are `format`,
which otamaker sets itself (see
[Artefact kinds and hashes](#artefact-kinds-and-hashes)), and
`security_version` (see [Security versions](#security-versions)).

Unrecognised fields are ignored. However, an artefact must either write to at
least one partition or include a script. `payloads` is shown here as a child of
`artefact`, which is the preferred layout, but a top-level `payloads` is also
accepted.

Although the contents file is a YAML document, only a simple subset of YAML is
supported. Stick to the structure above - anything clever is likely to fail.

Each payload is read from the file named by its `source` field, or from the
file with the same name as the payload if there is no `source`. Paths are
relative to the current directory, unless `-C <dir>` (`--directory`) gives
another, as for `tar`. `source` is only used when building: otamaker removes
it, and any comments, from the copy of the contents in the artefact. An older
form, a comment after the name (`- name: system # system.sparse`), is still
accepted when there is no `source`.

Payload names must be unique, and may not start with `_`, which is reserved
for the files otamaker adds.

If the first member of an archive is not `_contents_.yaml`, `_manifest_.yaml`
or `_manifest_.sig`, every member is treated as a sparse image for the
partition of the same name. This is a legacy form, which devices refuse once
they require manifests.

## Payloads

The following payload types are recognised:

* `image-sparse` - a _sparse image_ of a complete partition, which holds all
  the data in full but represents holes and blocks of fixed values efficiently.
  The name of the payload must match the name of a partition - `boot` and
  `system` for the `trixie-minbase-ab` images from `rpi-image-gen`. Images are
  written directly to the partition as they are downloaded, which saves time
  and means the device doesn't need space to store them.
* `eeprom-ab` - a Pi 5 A/B EEPROM update. This requires that an A/B-compatible
  EEPROM image is already installed, that the device has `rpi-connect-ota`
  1.3.14 or later and the `rpieepromab` package, and that `config.txt`
  includes `dtoverlay=nospi10`.
* `script` - a program to run on the device. See [Scripts](#scripts).
* `tmpfile` - another file to be copied to the device for the duration of the
  update, for use by scripts. It must appear before the scripts which use it.
* `tar` - a tar archive to be extracted on the device. See
  [Tar payloads](#tar-payloads).
* `hashlist` - added by otamaker for payloads which are hashed in chunks.
  Don't write these yourself.

### Tar payloads

A `tar` payload is an archive which is extracted using the system `tar`, at the
directory given by its `path` (`/` by default). The path must be absolute and
must already exist. The archive must not be compressed, as the artefact is
compressed anyway.

```yaml
  - name: overlay
    type: tar
    path: /opt/myapp
```

- Ownership and permissions are restored from the archive.
- Existing symlinks to directories (such as `/bin -> usr/bin`) are followed
  rather than replaced, and the metadata of existing directories is left
  unchanged.
- The archive is streamed into `tar` as it arrives, one verified chunk at a
  time, so it needs no space on the device beyond the files it extracts. A
  failed extraction, or a damaged chunk, aborts the update, but anything
  extracted before that is not rolled back.
- Because extracting an archive can do anything a script can, `tar` payloads
  are disabled along with scripts by `ExecuteScripts=false`.
- `tar` payloads need a manifest-only artefact, built with `-m`, and
  `rpi-connect-ota` 1.3.19 or later.

## Artefact kinds and hashes

A device only applies an artefact which matches a SHA256 hash that it has been
given separately, e.g. by Connect when you create a deployment. otamaker can
build three kinds of artefact, which differ in what that hash covers. Choose
one with `-t`, `-m` or `-l`.

* `-m` (`--manifest`) - a _manifest_ artefact. Its first member is
  `_manifest_.yaml`, which otamaker makes from the contents file, adding the
  `size` and SHA256 of every payload. The artefact is published by the hash
  of the manifest alone, which in turn covers everything else, so the device
  can check each payload as it arrives, before acting on it. A manifest can
  also be [signed](#signing). Devices need `rpi-connect-ota` 1.3.19 or later.
* `-l` (`--legacy`) - a _legacy_ artefact, whose first member is the contents
  file, renamed to `_contents_.yaml`. It is published by the hash of the whole
  archive, which can't be checked until all of it has been downloaded. By
  default, images are written as they arrive and scripts run only once the
  hash has been checked; a payload can override that with
  `pre_checksum: yes` or `no`.
* `-t` (`--transitional`) - a _transitional_ artefact. It is a manifest
  artefact with a `_contents_.yaml` added in front, so older devices install it
  as a legacy artefact and newer ones as a manifest artefact. It has two
  hashes: the hash of the whole archive, for older devices, and the hash of the
  manifest, for newer ones. It can only contain payloads that older devices
  can install, so not `tar`.

`-t` is the current default, but the default will become `-m`, so give the kind
explicitly in scripts which depend on it.

Payloads larger than the chunk size (`-c`, default 4M, up to 64M) are hashed in
chunks, with the digests in an extra `<name>.hashes` payload placed before
them, so that the device can check a large image as it is written. otamaker
also gives the manifest a `format` field, currently always 2. A `format` in the
contents file asks for at least that format, and `-f` (`--force`) allows an
unsupported one, e.g. to test devices. Don't put `size`, `sha256`, `chunk_size`
or hashlists in the contents file yourself.

otamaker prints the hashes when it finishes. `-w` (`--writesum`) also writes
the manifest hash to `<outfile>.manifest.sha256sum`, and the whole-archive hash
of a legacy or transitional artefact to `<outfile>.sha256sum`.

The manifest hash doesn't change between builds from the same contents and
payloads. The whole-archive hash normally does, because it depends on
timestamps, but if `SOURCE_DATE_EPOCH` is set in the environment then the
archive members are given that time, so the hash only changes when the
payloads do.

### Which hash to give

Give the manifest hash wherever you can. It is checked before anything is
applied, it is what raises the device's [security version](#security-versions),
and once that has happened the device refuses updates without it:

- A device with `rpi-connect-ota` 1.3.19 or later accepts either hash of a
  transitional artefact. Given the manifest hash, it checks every payload
  before using it. Given only the whole-archive hash, it applies the payloads
  as they arrive, and checks the hash at the end.
- A device with an older `rpi-connect-ota` only understands the whole-archive
  hash of a legacy or transitional artefact.
- Once a device has committed an update whose manifest it was able to verify
  (by its hash or by a trusted signature), it refuses legacy artefacts, and
  also manifest artefacts which can't be checked before they are applied, such
  as one given only its whole-archive hash. See
  [Security versions](#security-versions).

## Signing

A manifest can be signed, which signs everything it describes. A device with a
trusted keys file then refuses any artefact that isn't a manifest artefact
signed by one of its keys, so it can only be updated by its owner, whichever
server the artefact comes from. Devices without the file ignore signatures.

Signing uses ed25519 keys, which `openssl` can make:

```
$ openssl genpkey -algorithm ed25519 -out key.pem
$ otamaker --print-pubkey key.pem
```

`--print-pubkey` prints the line to add to the device's trusted keys file,
`/etc/rpi-connect/rpi-ota-trusted-keys`, plus the key id. The file holds one
key per line, and `#` starts a comment:

```
# Trusted OTA signing keys
ed25519 sap/qxtrQKeroIG0pQZyyiJhQXePglsuWA97nWJ6AkI= build-server
```

Then build with `-k`:

```
$ otamaker -m -k key.pem contents.yaml
```

`-k` can be repeated to sign with several keys, which is useful while keys are
being rotated - one valid signature from a trusted key is enough. The
signatures go in a `_manifest_.sig` member just before the manifest. Signing
works for transitional artefacts too, and older devices treat the signature as
a `tmpfile`.

If the private key isn't in a file otamaker can use, e.g. because it is in an
HSM, sign in two steps. Run otamaker with `--signed-data` to write the data to
be signed, sign it to produce a line of the form
`ed25519 <key id> <base64 signature>`, then run otamaker again with the same
contents and payloads, adding `-s` with the file holding that line:

```
$ otamaker -m --signed-data manifest.tbs contents.yaml
$ SIG=$(openssl pkeyutl -sign -rawin -inkey key.pem -in manifest.tbs | base64 -w 0)   # or your HSM's equivalent
$ echo "ed25519 <key id> $SIG" > manifest.sig
$ otamaker -m -s manifest.sig contents.yaml
```

where `<key id>` is as printed by `--print-pubkey`.

On a device with a trusted keys file:

- only manifest artefacts are accepted. Legacy artefacts are refused.
- the signature is checked before the manifest is read, whether or not a hash
  was given, and the artefact is refused unless it has a valid signature from a
  trusted key.
- every artefact is refused if the file can't be read or holds no keys.
- the file is read again for each artefact, so an update can install new keys,
  e.g. with a `tar` payload.

## Security versions

A signature or hash shows that an artefact is genuine, but not that it is the
latest. Without more, an old artefact with a known vulnerability could be
installed again. To prevent that, a device keeps a minimum _security version_,
and refuses artefacts below it. An artefact's security version is set by a
`security_version` field in its header:

```yaml
artefact:
  name: AwesomeOS-1.2
  version: 1.2
  security_version: 3
```

Raise `security_version` only for releases that fix security problems, so that
devices can still move freely between other releases.

The minimum is raised as follows (with `rpi-connect-ota` 1.3.22 or later):

- Every manifest artefact has a security version of at least 1, even if
  `security_version` is absent or 0. Legacy artefacts have none.
- When a manifest artefact whose security version is above the minimum is
  installed successfully, its security version becomes _pending_. The minimum
  is raised to it when the update is committed, i.e. once the update has
  completed and, if it needed a reboot, the device has restarted
  successfully.
- Only an artefact whose manifest was verified, by its hash or by a trusted
  signature, can raise the minimum.
- If the update fails, or the new image fails to boot and the device reverts to
  the old one, the pending value is discarded and the minimum is unchanged.
  Starting another update also discards it.

So the first update a device commits which was verified by its manifest hash or
by a signature stores a minimum of 1. From then on, the device behaves as
though `AllowLegacyArtefacts=false` (see [Device settings](#device-settings)):

- legacy artefacts are refused, as are raw images without a contents file or
  manifest.
- manifest artefacts are refused unless their manifest can be verified before
  any payload is applied - by the manifest hash, by a trusted signature, or by
  a whole-archive hash when the artefact has been downloaded in full before
  being installed. Devices install as they download by default, so in practice
  the manifest hash should be used.

The minimum is kept in `ota_security_version` on the boot partition
(`/bootfs`, or `/boot/firmware` on devices without A/B slots), outside the A/B
slots, so that an A/B update doesn't reset it. A pending value is kept beside
it, in `ota_security_version.pending`, so that it survives the reboot before
the update is committed. If `ota_security_version` exists but can't be read,
every artefact is refused.

To accept legacy artefacts again, e.g. to go back to an old release, or to
install an artefact with a lower security version, delete
`ota_security_version` on the device.

## Device settings

The device's update settings are in
`/etc/rpi-connect/rpi-ota-applicator.config`, as `Name=value` lines:

| Setting                      | Effect                                          |
|------------------------------|-------------------------------------------------|
| `ExecuteScripts=false`       | Refuse to run `script` payloads or extract `tar` payloads. |
| `AllowLegacyArtefacts=false` | Refuse legacy artefacts, and manifest artefacts which can't be verified before they are applied. This is implied once a [security version](#security-versions) has been stored. |

Adding a [trusted keys file](#signing) makes the device require signed manifest
artefacts.

## Scripts

Scripts will normally be written in one of the many scripting languages - shell,
Perl, Python 3, etc. - because they are largely independent of the device's
architecture, but they can be any program which runs on the device. An
interpreter, and any modules a script uses, must already be installed on the
device, and the script needs a standard "shebang" line (`#!/path/to/...`) so
that the device knows how to run it.

Script and tmpfile payloads are downloaded into a scratch directory, which is
deleted afterwards along with any other files created there. Scripts are run
with no parameters, from within the scratch directory, so they can refer to
earlier payloads without paths. A script runs only once all of it has been
downloaded and checked, unless a legacy artefact overrides that with
`pre_checksum: yes`.

The simplest script which uses a tmpfile would be something like:

```
#!/bin/sh
cat message
exit 0
```

where `message` is another, earlier payload, which may or may not hold the
first verse of _Never Gonna Give You Up_. This is not especially useful unless
you like filling your log files with Rick Astley lyrics, but if `message` is a
tar archive, it is an easy way to patch an existing image.

The script's exit code determines what happens next:

| Exit code | Meaning                                                          |
|-----------|------------------------------------------------------------------|
| 0         | Success                                                          |
| 2         | Success - a reboot is required                                   |
| 3         | Slot revert - tryboot into the other A/B slot, committing it as  |
|           | the default if the boot succeeds                                 |
| other     | Failure - the update is aborted                                  |

A slot revert does not modify the other slot, so it is a way of returning to
the previously installed image. It fails on a device without A/B slots.

Update scripts are run as root, so that they can modify filesystems and so
on. For this reason, be careful what you put in your scripts, and where you
build and host your artefacts. Scripts can be disabled with
`ExecuteScripts=false`.

## Example 1: Blinky

`blinky` is an artefact that flashes a Pi's LED, which can be useful if you
have several devices and want to be sure which is which. The script is only
made more complex by the desire to return the LED to its previous state. It
uses Bash, because Bash can count.

```bash
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

```yaml
artefact:
  name: blinky
  version: 1.0
  device_type: rpi

  payloads:
  - name: blinky
    type: script
```

Now create the artefact:

```
$ otamaker -t blinky.yaml
Contents:
  _contents_.yaml
  _manifest_.yaml
  blinky

Artefact: blinky.tar.zst
SHA256:   9fbb02297d0229893b61a44559737e462c94ceeb109e263cca45704caf0064c2 (whole artefact)
SHA256:   637c1526b9df4200f960fdf31f14f3496e5205b0a0dc9a651c508fcc5fca1a58 (manifest)
```

The contents file, although it was called `blinky.yaml`, has become
`_contents_.yaml`, and otamaker has added `_manifest_.yaml` because it built a
transitional artefact. Of the two hashes, use the manifest hash unless the
device's `rpi-connect-ota` is older than 1.3.19 (see
[Which hash to give](#which-hash-to-give)). The whole-artefact hash changes
with every build, because of the timestamps of the files otamaker gathers, but
the manifest hash only changes when the contents or the payloads do.

Artefacts can be hosted anywhere the device can reach. Find your computer's IP
address on your local network with `hostname -I` - on a home network it is
likely to be of the form `192.168.x.y`. Then start an HTTP server on it. This
wouldn't normally be the device you are updating, but that works too if you
only have one Pi.

```
$ python3 -m http.server 8000 --directory .
```

When you create the artefact in Connect, as part of adding a new deployment,
the URL will be something like `http://192.168.x.y:8000/blinky.tar.zst`,
substituting your own values for `x` and `y`. Copy the SHA256 exactly from the
output of otamaker.

Before clicking "Create and deploy" ("Deploy" if you have already created the
artefact), find the green LED on your Pi. Watch it as you click - it should
flash for a second or two.

## Example 2: An A/B update

This artefact updates an A/B image. It only works on an image with the
necessary partitions, built by a tool such as `rpi-image-gen`.

```yaml
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

This contents file is meant to be used from the root of the `rpi-image-gen`
repository. You could instead drop the paths, keeping `boot.sparse` and
`system.sparse`, and point otamaker at the `work/image-myapp-1.0.3` directory
(or your equivalent) with `-C`, or run it from there.

```
pi@philpi5:~/rpi-image-gen $ otamaker -t abupdate.yaml
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

The images are larger than the 4M chunk size, so each has a `.hashes` payload
before it holding the SHA256 of every chunk, and the device checks each chunk
before writing it to the partition. This artefact is much larger than blinky,
and may take a minute or more to install. At the end, the device reboots into
the new `boot` and `system` partitions, and marks the deployment as complete
when it reconnects.
