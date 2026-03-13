#!/usr/bin/env python3
"""
Build an OTA artefact from a contents YAML file.
Usage: otamaker [<options>] <contents file> [<outfile>]
"""

# Author: Phil Elwell <phil@raspberrypi.com>
# Copyright (c) 2026, Raspberry Pi Ltd.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions, and the following disclaimer,
#    without modification.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The names of the above-listed copyright holders may not be used
#    to endorse or promote products derived from this software without
#    specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
# IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import re
import sys
import argparse
import shutil
import subprocess
import tempfile

BLOCK_NONE = 0
BLOCK_HEADER = 1
BLOCK_PAYLOADS = 2

CONTENTS_FILE = '_contents_.yaml'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('contents',
                        help='the YAML contents file')
    parser.add_argument('outfile',
                        help="the artefact name (defaults to the 'name' in the contents)",
                        nargs='?')
    parser.add_argument('-w', '--writesum',
                        help='also write the sha256sum to <outfile>.sha256sum',
                        action='store_true')

    args = parser.parse_args()

    try:
        with open(args.contents, 'r', encoding='utf-8', errors='replace') as fh:
            lines = fh.readlines()
    except OSError as e:
        print(f"* failed to open '{args.contents}'")
        sys.exit(1)

    cwd = os.getcwd()
    state = BLOCK_NONE
    contents = []
    payloads = [CONTENTS_FILE]
    outfile = args.outfile
    errors = 0

    with tempfile.TemporaryDirectory() as tmpdir:
        for raw_line in lines:
            line = raw_line.rstrip('\r\n')
            comment = None
            if '#' in line:
                m = re.match(r'^([^#]+?)\s*#\s*(.*)$', line)
                if m:
                    line = m.group(1)
                    comment = m.group(2).strip()

            contents.append(line)

            if not line.strip():
                continue

            if line.strip() == 'artefact:':
                state = BLOCK_HEADER
            elif line.strip() == 'payloads:':
                state = BLOCK_PAYLOADS
            elif state == BLOCK_HEADER:
                name_m = re.match(r'^\s+name:\s*(.+)$', line)
                if name_m and args.outfile is None:
                    outfile = name_m.group(1).strip()
            elif state == BLOCK_PAYLOADS:
                # Normalise list item: '- name: x' -> '  name: x'
                payload_line = re.sub(r'^\s*-\s*', '  ', line)
                name_m = re.match(r'^\s+name:\s*(.+)$', payload_line)
                if name_m:
                    name = name_m.group(1).strip()
                    target = (comment if comment is not None else name).strip()
                    src = os.path.join(cwd, target)
                    dst = os.path.join(tmpdir, name)
                    if not os.path.isfile(src) and not os.path.islink(src) and not os.path.isdir(src):
                        if not os.path.exists(src):
                            print(f'* error: file {target} not found')
                            errors += 1
                    else:
                        try:
                            os.symlink(src, dst)
                            payloads.append(name)
                        except OSError:
                            if os.path.isfile(src):
                                shutil.copy2(src, dst)
                                payloads.append(name)
                            else:
                                print(f'* error: cannot symlink {target}')
                                errors += 1

        if errors:
            sys.exit(1)

        contents_path = os.path.join(tmpdir, CONTENTS_FILE)
        try:
            with open(contents_path, 'w', encoding='utf-8') as ofh:
                ofh.write('\n'.join(contents) + '\n')
        except OSError:
            print('* failed to create new contents file')
            sys.exit(1)

        if not outfile:
            print("* error: no output name (set 'name' in artefact header or pass as argument)")
            sys.exit(1)

        if not outfile.endswith('.tar.zst'):
            outfile += '.tar.zst'

        tar_cmd = [
            'tar', '--zstd', '-h', '-cvf', outfile, '-C', tmpdir
        ] + payloads
        r = subprocess.run(tar_cmd, capture_output=True)
        if r.returncode != 0:
            sys.exit(r.returncode)

        proc = subprocess.Popen(['sha256sum', outfile], stdout=subprocess.PIPE)
        line = proc.stdout.readline().decode().strip()
        sum = line[:64]
        if args.writesum:
            sum_path = f'{outfile}.sha256sum'
            with open(sum_path, 'w') as sf:
                print(sum, file=sf)

        print('Contents:')
        for payload in payloads:
            print(f'  {payload}')
        print()
        print(f'Artefact: {outfile}')
        print(f'SHA256:   {sum}')

if __name__ == '__main__':
    main()
