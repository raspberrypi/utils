#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only RP1 MAC PHC discovery. Never open a /dev/ptp* device."""
import argparse
import json
import os
from pathlib import Path
import shlex
import stat


def inventory(interface='eth0', sysfs=Path('/sys'), devices=Path('/dev')):
    net = (sysfs / 'class/net' / interface).resolve(strict=True)
    hardware = (net / 'device').resolve(strict=True)
    driver = (hardware / 'driver').resolve(strict=True).name
    if hardware.name != '1f00100000.ethernet':
        raise ValueError('Interface is not the supported RP1 Ethernet device')
    result = {'interface': interface, 'hardware': str(hardware), 'driver': driver,
              'clocks': [], 'skipped': []}
    for entry in sorted((sysfs / 'class/ptp').glob('ptp*')):
        try:
            name = (entry / 'clock_name').read_text().strip()
            parent = (entry / 'device').resolve(strict=True)
            dev_t = tuple(map(int, (entry / 'dev').read_text().strip().split(':')))
            node = devices / entry.name
            node_stat = node.stat()
            if (len(dev_t) != 2 or not stat.S_ISCHR(node_stat.st_mode) or
                    dev_t != (os.major(node_stat.st_rdev), os.minor(node_stat.st_rdev))):
                raise ValueError('Character device does not match sysfs identity')
            row = {'name': name, 'device': str(node), 'parent': str(parent),
                   'device_number': list(dev_t),
                   'rp1_mac': name == 'gem-ptp-timer' and parent in (net, hardware)}
            if row['rp1_mac']:
                row['capabilities'] = {
                    key: int((entry / attr).read_text()) for key, attr in
                    [('extts', 'n_external_timestamps'),
                     ('perout', 'n_periodic_outputs'), ('pins', 'n_programmable_pins')]}
            result['clocks'].append(row)
        except (OSError, ValueError) as exc:
            result['skipped'].append({'entry': entry.name, 'reason': str(exc)})
    return result


def select_mac(info):
    matches = [row for row in info['clocks'] if row['rp1_mac']]
    if len(matches) != 1:
        raise ValueError(f'Expected one live RP1 MAC PHC, found {len(matches)}')
    return matches[0]


def command_plan(info, input_gpio, output_gpio, testptp, input_gpio2=None):
    if not (0 <= input_gpio <= 27 and 0 <= output_gpio <= 27):
        raise ValueError('Select GPIO numbers in 0..27, not physical header pin numbers')
    if input_gpio == output_gpio:
        raise ValueError('Select different input and output GPIOs')
    if input_gpio2 is not None and (
            not 0 <= input_gpio2 <= 27 or input_gpio2 in (input_gpio, output_gpio)):
        raise ValueError('Select a distinct second input GPIO in 0..27')
    mac = select_mac(info)
    if info['driver'] not in ('macb', 'macb_pps') or mac['capabilities'] != {
            'extts': 2, 'perout': 1, 'pins': 28}:
        raise ValueError('The active PHC does not expose two EXTTS channels and one PEROUT channel')
    base = [testptp, '-d', mac['device'], '-i', '0']
    input1 = [testptp, '-d', mac['device'], '-i', '0']
    input2 = [testptp, '-d', mac['device'], '-i', '1']
    mapping = [shlex.join(base + ['-L', f'{input_gpio},1']),
               shlex.join(base + ['-L', f'{output_gpio},2'])]
    result = {
        'device': mac['device'],
        'note': 'Commands are printed only. EXTTS channels 0 and 1 and PEROUT can run together.',
        'mapping': mapping,
        'input_example': shlex.join(input1 + ['-E', '1', '-e', '10']),
        'input_stop': shlex.join(base + ['-L', f'{input_gpio},0']),
        'input_stop_note': 'Stops capture by clearing its pin mapping. Reapply the input mapping before restarting.',
        'output_start': shlex.join(base + ['-p', '1000000000', '-H', '0', '-w', '10000000']),
        'output_stop': shlex.join(base + ['-p', '0']),
    }
    if input_gpio2 is not None:
        result['mapping'].insert(1, shlex.join(input2 + ['-L', f'{input_gpio2},1']))
        result['second_input_example'] = shlex.join(input2 + ['-E', '1', '-e', '10'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--interface', default='eth0')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--device', action='store_true', help='Print only the discovered MAC device')
    mode.add_argument('--commands', action='store_true', help='Print standard testptp commands')
    parser.add_argument('--input-gpio', type=int, default=18)
    parser.add_argument('--input-gpio2', type=int)
    parser.add_argument('--output-gpio', type=int, default=23)
    parser.add_argument('--testptp', default='testptp')
    args = parser.parse_args()
    try:
        info = inventory(args.interface)
        if args.device:
            print(select_mac(info)['device'])
        else:
            print(json.dumps(command_plan(info, args.input_gpio, args.output_gpio,
                                          args.testptp, args.input_gpio2)
                             if args.commands else info, indent=2))
    except (OSError, ValueError) as exc:
        parser.exit(1, f'{exc}\n')


if __name__ == '__main__':
    main()
