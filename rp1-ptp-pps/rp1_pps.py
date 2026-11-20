#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Guarded RP1 PPS control and qualification using the standard testptp CLI.

The default action is plan-only. Use --execute to make a pin mapping or start
PPS. GPIO values are RP1/BCM numbers 0..27, not physical header pin numbers.
Duplex mode uses testptp for simultaneous PEROUT and one or two EXTTS channels.
Use a positive event count for qualification or zero for supervised service.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import fcntl
import json
import os
from pathlib import Path
import queue
import re
import shlex
import signal
import struct
import subprocess
import sys
import threading
import time

from phc_identity import inventory, select_mac


CAPABILITY_PATTERNS = {
    'extts': re.compile(r'^\s*(\d+) external time stamp channels$'),
    'perout': re.compile(r'^\s*(\d+) programmable periodic signals$'),
    'pins': re.compile(r'^\s*(\d+) programmable pins$'),
}
PIN_PATTERN = re.compile(r'^name (.+) index (\d+) func (\d+) chan (\d+)$')
EVENT_PATTERN = re.compile(r'^event index (\d+) at (\d+)\.(\d{9})$')
ERROR_PREFIXES = ('opening ', 'clock_adjtime:', 'PTP_PIN_SETFUNC:',
                  'PTP_PIN_GETFUNC:',
                  'PTP_EXTTS_REQUEST', 'PTP_PEROUT_REQUEST:', 'read:')


def _ioc(direction, type_, number, size):
    return (direction << 30) | (size << 16) | (type_ << 8) | number


PTP_EXTTS_REQUEST_IOCTL = _ioc(1, ord('='), 2, 16)


class PHCUnavailable(RuntimeError):
    """The network PHC has not registered yet and may appear later."""


def _optional_gpio(value):
    if value.lower() in ('none', 'off'):
        return None
    return int(value)


class RP1PPS:
    def __init__(self, *, interface='eth0', testptp='testptp',
                 inventory_fn=None, monotonic=None, sleep=None,
                 popen_factory=None, output_monitor_interval=1.0,
                 input_retry_delay=1.0, extts_disable_fn=None, ownership_file=None,
                 boot_id_fn=None):
        self.interface = interface
        self.testptp = str(testptp)
        if output_monitor_interval <= 0:
            raise ValueError('output monitor interval must be positive')
        if input_retry_delay <= 0:
            raise ValueError('input retry delay must be positive')
        self.inventory_fn = inventory_fn or inventory
        self.monotonic = monotonic or time.monotonic
        self.sleep = sleep or time.sleep
        self.popen_factory = popen_factory or subprocess.Popen
        self.output_monitor_interval = output_monitor_interval
        self.input_retry_delay = input_retry_delay
        self.extts_disable_fn = extts_disable_fn or self._disable_extts_request
        self.ownership_file = Path(ownership_file) if ownership_file else None
        self.boot_id_fn = boot_id_fn or self._read_boot_id
        self.owned_mappings = {}
        self.clock = None
        self.capabilities = None
        self.pins = None

    @staticmethod
    def _read_boot_id():
        return Path('/proc/sys/kernel/random/boot_id').read_text().strip()

    def _save_owned_mappings(self):
        if self.ownership_file is None:
            return
        if not self.owned_mappings:
            self.ownership_file.unlink(missing_ok=True)
            return
        self.ownership_file.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        record = {'interface': self.interface, 'phc': self.clock['device'],
                  'boot_id': self.boot_id_fn(),
                  'mappings': sorted(self.owned_mappings.values(),
                                     key=lambda row: row['gpio'])}
        temporary = self.ownership_file.with_name(
            self.ownership_file.name + f'.{os.getpid()}.tmp')
        temporary.write_text(json.dumps(record, sort_keys=True) + '\n')
        temporary.chmod(0o600)
        temporary.replace(self.ownership_file)

    def _track_mapping(self, gpio, function, channel):
        self.owned_mappings[gpio] = {'gpio': gpio, 'function': function,
                                     'channel': channel}
        self._save_owned_mappings()

    def _forget_mapping(self, gpio):
        self.owned_mappings.pop(gpio, None)
        self._save_owned_mappings()

    def cleanup_owned_mappings(self, expected):
        """Clean only the exact mapping set previously recorded by this service."""
        if self.ownership_file is None or not self.ownership_file.exists():
            return {'cleaned': False, 'reason': 'no owned mapping record'}
        try:
            record = json.loads(self.ownership_file.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            raise RuntimeError(f'Cannot read PPS ownership record: {exc}') from exc
        if not isinstance(record, dict):
            raise RuntimeError('PPS ownership record is not a JSON object')
        mappings = record.get('mappings')
        if mappings is None and {'mode', 'gpio'} <= record.keys():
            mappings = [{'gpio': record['gpio'],
                         'function': 1 if record['mode'] == 'input' else 2,
                         'channel': 0}]
        if not isinstance(mappings, list):
            raise RuntimeError('PPS ownership record has no valid mapping list')
        normalize = lambda rows: sorted(
            ({'gpio': row.get('gpio'), 'function': row.get('function'),
              'channel': row.get('channel', 0)} for row in rows),
            key=lambda row: row['gpio'])
        if (record.get('interface') != self.interface or
                normalize(mappings) != normalize(expected)):
            return {'cleaned': False, 'reason': 'ownership record mismatch'}
        try:
            boot_id = self.boot_id_fn()
        except OSError as exc:
            raise RuntimeError(f'Cannot verify boot identity for PPS cleanup: {exc}') from exc
        if record.get('boot_id') != boot_id:
            self.ownership_file.unlink(missing_ok=True)
            return {'cleaned': False, 'reason': 'record belongs to an earlier boot'}

        try:
            self._discover_phc()
        except PHCUnavailable as exc:
            return {'cleaned': False, 'reason': f'PHC unavailable: {exc}'}
        if record.get('phc') != self.clock['device']:
            return {'cleaned': False, 'reason': 'PHC identity changed'}
        pins = self._read_pin_map()
        expected_by_gpio = {row['gpio']: row for row in mappings}
        occupied = {gpio: row for gpio, row in pins.items()
                    if row['function'] != 0}
        unexpected = set(occupied) - set(expected_by_gpio)
        if unexpected:
            raise RuntimeError(f'PPS cleanup found unrelated PTP pin mappings: '
                               f'{sorted(unexpected)}')
        for gpio, actual in occupied.items():
            owner = expected_by_gpio[gpio]
            if (actual['function'] != owner['function'] or
                    actual['channel'] != owner['channel']):
                raise RuntimeError(f'PPS cleanup ownership mismatch on GPIO{gpio}')

        # Disable timestamp requests before stopping their testptp readers or
        # releasing GPIOs. This also recovers a daemon that died unexpectedly.
        for row in mappings:
            if row['function'] == 1:
                self.extts_disable_fn(row['channel'])
        if any(row['function'] == 2 for row in mappings):
            self._command('-p', '0', timeout=10,
                          acknowledgments=('periodic output request okay',))
        for row in reversed(mappings):
            if row['gpio'] in occupied:
                self._command('-L', f"{row['gpio']},0", index=row['channel'],
                              timeout=10,
                              acknowledgments=('set pin function okay',))
        self.ownership_file.unlink(missing_ok=True)
        return {'cleaned': True, 'mappings': mappings}

    def _disable_extts_request(self, channel):
        """Disable a live standard PTP EXTTS request before stopping its reader."""
        fd = os.open(self.clock['device'], os.O_RDWR | os.O_CLOEXEC)
        try:
            request = bytearray(struct.pack('=IIII', channel, 0, 0, 0))
            fcntl.ioctl(fd, PTP_EXTTS_REQUEST_IOCTL, request, True)
        finally:
            os.close(fd)

    def _command(self, *options, index=0, timeout=10, acknowledgments=()):
        if self.clock is None:
            raise RuntimeError('Discover the RP1 PHC before running testptp')
        argv = [self.testptp, '-d', self.clock['device'], '-i', str(index),
                *map(str, options)]
        child = self.popen_factory(argv, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        try:
            stdout, stderr = child.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            child.terminate()
            try:
                stdout, stderr = child.communicate(timeout=2)
            except subprocess.TimeoutExpired:
                child.kill()
                stdout, stderr = child.communicate()
            raise TimeoutError(f'testptp timed out: {shlex.join(argv)}\n{stdout}{stderr}')
        except KeyboardInterrupt:
            child.terminate()
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
            raise
        output = stdout + stderr
        if child.returncode:
            raise RuntimeError(f'testptp exited {child.returncode}: {output.strip()}')
        if any(line.startswith(ERROR_PREFIXES) for line in output.splitlines()):
            raise RuntimeError(f'testptp reported an ioctl/read error: {output.strip()}')
        for expected in acknowledgments:
            if expected not in stdout.splitlines():
                raise RuntimeError(f'testptp did not acknowledge {expected!r}: {output.strip()}')
        return stdout

    def _discover_phc(self):
        try:
            info = self.inventory_fn(self.interface)
        except OSError as exc:
            raise PHCUnavailable(str(exc)) from exc
        if not any(row.get('rp1_mac') for row in info.get('clocks', [])):
            raise PHCUnavailable('No live RP1 Ethernet PHC is registered')
        self.clock = select_mac(info)
        if info['driver'] not in ('macb', 'macb_pps'):
            raise RuntimeError(f"Unexpected RP1 MAC PHC driver: {info['driver']!r}")
        expected = {'extts': 2, 'perout': 1, 'pins': 28}
        if self.clock.get('capabilities', {}) != expected:
            raise RuntimeError(f'Unexpected RP1 PPS capabilities: '
                               f'{self.clock.get("capabilities", {})!r}')
        return info, expected

    def _read_pin_map(self):
        pin_output = self._command('-l')
        pins = {}
        for line in pin_output.splitlines():
            match = PIN_PATTERN.match(line)
            if match:
                name, index, function, channel = match.groups()
                pins[int(index)] = {'name': name, 'function': int(function),
                                    'channel': int(channel)}
        if set(pins) != set(range(28)):
            raise RuntimeError(f'testptp returned an incomplete pin map: {sorted(pins)}')
        return pins

    def preflight(self):
        info, expected = self._discover_phc()
        cap_output = self._command('-c')
        observed = {}
        for line in cap_output.splitlines():
            for key, pattern in CAPABILITY_PATTERNS.items():
                match = pattern.match(line)
                if match:
                    observed[key] = int(match.group(1))
        if observed != expected:
            raise RuntimeError(f'testptp capabilities differ from sysfs: {observed!r}')
        pins = self._read_pin_map()
        occupied = {gpio: row for gpio, row in pins.items() if row['function'] != 0}
        if occupied:
            raise RuntimeError(f'Existing PTP pin mappings must be cleared first: {occupied}')
        self.capabilities, self.pins = observed, pins
        return {'interface': self.interface, 'driver': info['driver'],
                'phc': self.clock['device'], 'capabilities': observed,
                'pins': pins}

    def plan(self, mode, gpio, *, edge='rising', events=4,
             period_ns=1_000_000_000, high_ns=10_000_000,
             phase_ns=0, duration=None, timeout=None,
             event_timeout=3.0, channel=0):
        if mode not in ('input', 'output'):
            raise ValueError('mode must be input or output')
        if not isinstance(gpio, int) or not 0 <= gpio < 28:
            raise ValueError('GPIO must be an RP1/BCM header GPIO in 0..27')
        if self.pins is None:
            raise RuntimeError('Run preflight before creating a plan')
        channel_count = self.capabilities['extts'] if mode == 'input' else 1
        if not isinstance(channel, int) or not 0 <= channel < channel_count:
            if mode == 'input':
                raise ValueError(f'EXTTS channel must be in 0..{channel_count - 1}')
            raise ValueError('PEROUT channel must be 0')
        if mode == 'input' and channel == 1:
            raise ValueError('EXTTS channel 1 requires active PEROUT; use duplex mode')
        function = 1 if mode == 'input' else 2
        base = [self.testptp, '-d', self.clock['device'], '-i', str(channel)]
        mapping = ['-L', f'{gpio},{function}']
        if mode == 'input':
            if edge not in ('rising', 'falling'):
                raise ValueError('edge must be rising or falling')
            if events < 0:
                raise ValueError('events must be zero for continuous capture or positive')
            testptp_events = 2147483647 if events == 0 else events
            capture = ['-E', '1' if edge == 'rising' else '2',
                       '-e', str(testptp_events)]
            return {'mode': mode, 'gpio': gpio, 'channel': channel, 'edge': edge,
                    'events': events, 'continuous': events == 0,
                    'mapping': shlex.join(base + mapping),
                    'capture': shlex.join(base + mapping + capture),
                    'cleanup': shlex.join(base + ['-L', f'{gpio},0']),
                    'timeout_s': (event_timeout if events == 0 else
                                  timeout if timeout is not None else max(5, events * 3))}
        if period_ns < 1 or high_ns < 1 or high_ns >= period_ns:
            raise ValueError('require 0 < high-ns < period-ns')
        if phase_ns < 0 or phase_ns >= period_ns:
            raise ValueError('phase-ns must be in [0, period-ns)')
        start = ['-p', str(period_ns), '-H', str(phase_ns), '-w', str(high_ns)]
        return {'mode': mode, 'gpio': gpio, 'period_ns': period_ns,
                'high_ns': high_ns, 'phase_ns': phase_ns,
                'mapping': shlex.join(base + mapping),
                'start': shlex.join(base + mapping + start),
                'stop': shlex.join(base + ['-p', '0']),
                'cleanup': shlex.join(base + ['-L', f'{gpio},0']),
                'duration_s': duration}

    def plan_duplex(self, input_gpio, output_gpio, *, input_channel=0,
                    input_gpio2=None,
                    edge='rising', events=4,
                    period_ns=1_000_000_000, high_ns=10_000_000,
                    phase_ns=0, timeout=None, event_timeout=5.0):
        if self.pins is None:
            raise RuntimeError('Run preflight before creating a plan')
        if not isinstance(input_channel, int) or not 0 <= input_channel < self.capabilities['extts']:
            raise ValueError(f'EXTTS channel must be in 0..{self.capabilities["extts"] - 1}')
        if not isinstance(input_gpio, int) or not 0 <= input_gpio < 28:
            raise ValueError('Input GPIO must be an RP1/BCM GPIO in 0..27')
        if not isinstance(output_gpio, int) or not 0 <= output_gpio < 28:
            raise ValueError('Output GPIO must be an RP1/BCM GPIO in 0..27')
        if input_gpio == output_gpio:
            raise ValueError('Duplex input and output must use different GPIOs')
        if input_gpio2 is not None:
            if input_channel != 0:
                raise ValueError('Two-input duplex uses EXTTS channels 0 and 1')
            if not isinstance(input_gpio2, int) or not 0 <= input_gpio2 < 28:
                raise ValueError('Second input GPIO must be an RP1/BCM GPIO in 0..27')
            if input_gpio2 in (input_gpio, output_gpio):
                raise ValueError('Each duplex direction must use a different GPIO')
        if edge != 'rising':
            raise ValueError('duplex counter lanes currently support rising edges only')
        if events < 0:
            raise ValueError('events must be zero for continuous duplex or positive')
        if period_ns < 1 or high_ns < 1 or high_ns >= period_ns:
            raise ValueError('require 0 < high-ns < period-ns')
        if phase_ns < 0 or phase_ns >= period_ns:
            raise ValueError('phase-ns must be in [0, period-ns)')
        if event_timeout <= 0 or (timeout is not None and timeout <= 0):
            raise ValueError('timeouts must be positive')
        testptp_events = 2147483647 if events == 0 else events
        base = [self.testptp, '-d', self.clock['device'], '-i', '0']
        input_base = [self.testptp, '-d', self.clock['device'],
                      '-i', str(input_channel)]
        plan = {
            'mode': 'duplex', 'input_gpio': input_gpio,
            'input_channel': input_channel,
            'output_gpio': output_gpio, 'edge': edge, 'events': events,
            'period_ns': period_ns, 'high_ns': high_ns,
            'phase_ns': phase_ns,
            'map_output': shlex.join(base + ['-L', f'{output_gpio},2']),
            'map_input': shlex.join(input_base + ['-L', f'{input_gpio},1']),
            'start_output': shlex.join(base + ['-p', str(period_ns),
                '-H', str(phase_ns), '-w', str(high_ns)]),
            'capture_input': shlex.join(input_base + ['-E',
                '1' if edge == 'rising' else '2', '-e', str(testptp_events)]),
            'stop_output': shlex.join(base + ['-p', '0']),
            'clear_input': shlex.join(input_base + ['-L', f'{input_gpio},0']),
            'clear_output': shlex.join(base + ['-L', f'{output_gpio},0']),
            'timeout_s': (timeout if timeout is not None else
                          (None if events == 0 else max(5, events * 3))),
            'continuous': events == 0,
            'event_timeout_s': event_timeout,
        }
        if input_gpio2 is not None:
            second_base = [self.testptp, '-d', self.clock['device'], '-i', '1']
            plan['input_gpio2'] = input_gpio2
            plan['map_input2'] = shlex.join(
                second_base + ['-L', f'{input_gpio2},1'])
            plan['capture_input2'] = shlex.join(second_base + ['-E',
                '1' if edge == 'rising' else '2', '-e', str(testptp_events)])
            plan['clear_input2'] = shlex.join(
                second_base + ['-L', f'{input_gpio2},0'])
        return plan

    def _capture_continuous(self, edge, *, channel=0, event_timeout,
                            event_callback=None, cleanup_state=None):
        argv = [self.testptp, '-d', self.clock['device'], '-i', str(channel),
                '-E', '1' if edge == 'rising' else '2', '-e', '2147483647']
        child = self.popen_factory(argv, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True,
                                   bufsize=1)
        lines = queue.Queue()

        def read_lines():
            try:
                for line in child.stdout:
                    lines.put(line.rstrip('\r\n'))
            finally:
                lines.put(None)

        reader = threading.Thread(target=read_lines, daemon=True)
        reader.start()
        events_received = 0
        acknowledged = False
        last_event = time.monotonic()
        stopped_by = 'process-exit'
        cleanup_state = cleanup_state if cleanup_state is not None else {}
        cleanup_state['active'] = True
        try:
            while True:
                remaining = event_timeout - (time.monotonic() - last_event)
                if remaining <= 0:
                    raise TimeoutError(f'No PPS input event received for {event_timeout:g} seconds')
                try:
                    line = lines.get(timeout=remaining)
                except queue.Empty:
                    raise TimeoutError(f'No PPS input event received for {event_timeout:g} seconds')
                if line is None:
                    child.wait()
                    raise RuntimeError('testptp stopped while continuous input capture was active')
                if line.startswith(ERROR_PREFIXES):
                    raise RuntimeError(f'testptp reported an input error: {line}')
                if line == 'external time stamp request okay':
                    acknowledged = True
                    continue
                match = EVENT_PATTERN.match(line)
                if not match:
                    continue
                if not acknowledged:
                    raise RuntimeError('testptp returned an event without acknowledging EXTS setup')
                event_channel, sec, nsec = match.groups()
                if int(event_channel) != int(argv[argv.index('-i') + 1]):
                    raise RuntimeError(f'Unexpected PTP timestamp channel {event_channel}')
                event = {'channel': int(event_channel),
                         'timestamp_ns': int(sec) * 1_000_000_000 + int(nsec)}
                events_received += 1
                last_event = time.monotonic()
                if event_callback:
                    event_callback(event)
        except KeyboardInterrupt:
            stopped_by = 'signal'
        finally:
            disable_error = None
            if cleanup_state.get('active'):
                try:
                    self.extts_disable_fn(channel)
                    cleanup_state['active'] = False
                except Exception as exc:
                    disable_error = exc
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
            reader.join(timeout=2)
            if child.stdout:
                child.stdout.close()
            if disable_error:
                raise RuntimeError(
                    f'EXTTS channel {channel} disable failed; retaining its '
                    f'GPIO mapping: {disable_error}')
        return {'events_received': events_received, 'stopped_by': stopped_by}

    def capture(self, gpio, *, channel=0, edge='rising', events=4, timeout=None,
                event_timeout=3.0, event_callback=None):
        plan = self.plan('input', gpio, edge=edge, events=events,
                         timeout=timeout, event_timeout=event_timeout,
                         channel=channel)
        timeout = plan['timeout_s']
        mapped = False
        mapping_attempted = False
        continuous_state = {'active': False}
        request_state = {'active': False}
        try:
            mapping_attempted = True
            self._track_mapping(gpio, 1, channel)
            self._command('-L', f'{gpio},1', index=channel, timeout=10,
                          acknowledgments=('set pin function okay',))
            mapped = True
            if events == 0:
                events_received = 0
                input_inactive = False

                def report_event(event):
                    nonlocal events_received, input_inactive
                    events_received += 1
                    if input_inactive:
                        print('PPS input resumed', file=sys.stderr, flush=True)
                        input_inactive = False
                    if event_callback:
                        event_callback(event)

                while True:
                    try:
                        capture = self._capture_continuous(edge, channel=channel,
                            event_timeout=event_timeout,
                            event_callback=report_event,
                            cleanup_state=continuous_state)
                    except TimeoutError as exc:
                        if not input_inactive:
                            print(f'{exc}; rearming input capture in '
                                  f'{self.input_retry_delay:g} seconds',
                                  file=sys.stderr, flush=True)
                            input_inactive = True
                        try:
                            self.sleep(self.input_retry_delay)
                        except KeyboardInterrupt:
                            return {'passed': True, 'mode': 'input',
                                    'gpio': gpio, 'channel': channel,
                                    'edge': edge, 'continuous': True,
                                    'events_received': events_received,
                                    'stopped_by': 'signal'}
                        continue
                    return {'passed': True, 'mode': 'input',
                            'gpio': gpio, 'channel': channel,
                            'edge': edge, 'continuous': True,
                            'events_received': events_received,
                            'stopped_by': capture['stopped_by']}
            request_state['active'] = True
            output = self._command('-E', '1' if edge == 'rising' else '2',
                '-e', str(events),
                index=channel,
                timeout=timeout,
                acknowledgments=('external time stamp request okay',))
            request_state['active'] = False
            records = []
            for line in output.splitlines():
                match = EVENT_PATTERN.match(line)
                if match:
                    event_channel, sec, nsec = match.groups()
                    records.append({'channel': int(event_channel),
                                    'timestamp_ns': int(sec) * 1_000_000_000 + int(nsec)})
            if len(records) != events or any(row['channel'] != channel for row in records):
                raise RuntimeError(f'Expected {events} channel-{channel} timestamps, received {records!r}')
            return {'passed': True, 'mode': 'input', 'gpio': gpio,
                    'channel': channel, 'edge': edge,
                    'events': records, 'testptp_output': output}
        finally:
            disable_error = None
            if request_state['active']:
                try:
                    self.extts_disable_fn(channel)
                    request_state['active'] = False
                except Exception as exc:
                    disable_error = exc
            if mapping_attempted and not continuous_state['active'] and not request_state['active']:
                self._command('-L', f'{gpio},0', index=channel, timeout=10,
                              acknowledgments=('set pin function okay',))
                self._forget_mapping(gpio)
            if disable_error:
                raise RuntimeError(
                    f'EXTTS channel {channel} disable failed; retaining its '
                    f'GPIO mapping: {disable_error}')

    def _verify_output_mapping(self, gpio):
        output = self._command('-l', timeout=10)
        pins = {}
        for line in output.splitlines():
            match = PIN_PATTERN.match(line)
            if match:
                name, index, function, channel = match.groups()
                pins[int(index)] = {'name': name, 'function': int(function),
                                    'channel': int(channel)}
        if set(pins) != set(range(28)):
            raise RuntimeError('PTP pin map became incomplete while PPS output was active')
        occupied = {index: row for index, row in pins.items()
                    if row['function'] and index != gpio}
        if occupied:
            raise RuntimeError(f'Other PTP pin mappings appeared during output: {occupied}')
        if pins[gpio]['function'] != 2 or pins[gpio]['channel'] != 0:
            raise RuntimeError(f'GPIO{gpio} lost its PEROUT mapping while output was active')

    def _verify_duplex_mappings(self, input_gpio, output_gpio, input_channel=0,
                                other_inputs=()):
        output = self._command('-l', timeout=10)
        pins = {}
        for line in output.splitlines():
            match = PIN_PATTERN.match(line)
            if match:
                name, index, function, channel = match.groups()
                pins[int(index)] = {'name': name, 'function': int(function),
                                    'channel': int(channel)}
        if set(pins) != set(range(28)):
            raise RuntimeError('PTP pin map became incomplete during duplex PPS')
        expected = {input_gpio: (1, input_channel), output_gpio: (2, 0)}
        expected.update({gpio: (1, channel) for gpio, channel in other_inputs})
        occupied = {index: row for index, row in pins.items()
                    if row['function'] and index not in expected}
        if occupied:
            raise RuntimeError(f'Unexpected PTP pin mappings during duplex PPS: {occupied}')
        for gpio, (function, channel) in expected.items():
            if pins[gpio]['function'] != function or pins[gpio]['channel'] != channel:
                label = 'EXTTS' if function == 1 else 'PEROUT'
                raise RuntimeError(f'GPIO{gpio} lost its {label} mapping during duplex PPS')

    def _capture_duplex_events(self, input_gpio, output_gpio, edge, events, *,
                               input_channel=0, timeout,
                               event_timeout, event_callback=None,
                               other_inputs=(), cleanup_state=None):
        argv = [self.testptp, '-d', self.clock['device'], '-i', str(input_channel),
                '-E', '1' if edge == 'rising' else '2', '-e', str(events)]
        child = self.popen_factory(argv, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True,
                                   bufsize=1)
        lines = queue.Queue()

        def read_lines():
            try:
                for line in child.stdout:
                    lines.put(line.rstrip('\r\n'))
            finally:
                lines.put(None)

        reader = threading.Thread(target=read_lines, daemon=True)
        reader.start()
        records = []
        acknowledged = False
        start = time.monotonic()
        last_event = start
        next_mapping_check = start + self.output_monitor_interval
        stopped = False
        cleanup_state = cleanup_state if cleanup_state is not None else {
            'active_channels': set()}
        cleanup_state['active_channels'].add(input_channel)
        try:
            while len(records) < events:
                now = time.monotonic()
                if now - start >= timeout:
                    raise TimeoutError(f'testptp input exceeded {timeout:g} seconds')
                if now - last_event >= event_timeout:
                    raise TimeoutError(f'No PPS input event received for {event_timeout:g} seconds')
                if now >= next_mapping_check:
                    self._verify_duplex_mappings(input_gpio, output_gpio,
                                                 input_channel, other_inputs)
                    next_mapping_check = time.monotonic() + self.output_monitor_interval
                wait = min(0.2, timeout - (now - start),
                           event_timeout - (now - last_event))
                try:
                    line = lines.get(timeout=max(0.001, wait))
                except queue.Empty:
                    continue
                if line is None:
                    child.wait()
                    stopped = True
                    break
                if line.startswith(ERROR_PREFIXES):
                    raise RuntimeError(f'testptp reported an input error: {line}')
                if line == 'external time stamp request okay':
                    acknowledged = True
                    continue
                match = EVENT_PATTERN.match(line)
                if not match:
                    continue
                if not acknowledged:
                    raise RuntimeError('testptp returned an event without acknowledging EXTS setup')
                channel, sec, nsec = match.groups()
                if int(channel) != input_channel:
                    raise RuntimeError(f'Unexpected PTP timestamp channel {channel}')
                event = {'channel': input_channel,
                         'timestamp_ns': int(sec) * 1_000_000_000 + int(nsec)}
                records.append(event)
                last_event = time.monotonic()
                if event_callback:
                    event_callback(event)
            if len(records) != events:
                raise RuntimeError(f'Expected {events} channel-{input_channel} timestamps, received {records!r}')
            if not acknowledged:
                raise RuntimeError('testptp did not acknowledge EXTS setup')
            if not stopped:
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    raise TimeoutError('testptp did not disable EXTTS after the requested events')
                stopped = True
            if child.returncode:
                raise RuntimeError(f'testptp exited {child.returncode} during EXTTS capture')
            self.extts_disable_fn(input_channel)
            cleanup_state['active_channels'].discard(input_channel)
            return records
        finally:
            disable_error = None
            if input_channel in cleanup_state['active_channels']:
                try:
                    self.extts_disable_fn(input_channel)
                    cleanup_state['active_channels'].discard(input_channel)
                except Exception as exc:
                    disable_error = exc
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
            reader.join(timeout=2)
            if child.stdout:
                child.stdout.close()
            if disable_error:
                raise RuntimeError(
                    f'EXTTS channel {input_channel} disable failed; retaining '
                    f'its GPIO mapping: {disable_error}')

    def _capture_duplex_continuous(self, inputs, output_gpio, edge, *,
                                   event_timeout, event_callback=None,
                                   cleanup_state=None):
        """Supervise both EXTTS readers and rearm inputs without stopping PEROUT."""
        cleanup_state = (cleanup_state if cleanup_state is not None else
                         {'active_channels': set()})
        channels = [channel for _gpio, channel in inputs]
        counts = {channel: 0 for channel in channels}
        input_inactive = False

        while True:
            lines = queue.Queue()
            children = {}
            readers = []
            acknowledged = set()
            last_event = {channel: time.monotonic() for channel in channels}
            next_mapping_check = time.monotonic() + self.output_monitor_interval
            outcome = None

            def read_lines(channel, child):
                try:
                    for line in child.stdout:
                        lines.put((channel, line.rstrip('\r\n')))
                finally:
                    lines.put((channel, None))

            try:
                for gpio, channel in inputs:
                    argv = [self.testptp, '-d', self.clock['device'],
                            '-i', str(channel), '-E',
                            '1' if edge == 'rising' else '2',
                            '-e', '2147483647']
                    child = self.popen_factory(argv, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, text=True, bufsize=1)
                    children[channel] = child
                    cleanup_state['active_channels'].add(channel)
                    reader = threading.Thread(target=read_lines,
                        args=(channel, child), daemon=True)
                    readers.append(reader)
                    reader.start()

                while True:
                    now = time.monotonic()
                    expired = [channel for channel in channels
                               if now - last_event[channel] >= event_timeout]
                    if expired:
                        raise TimeoutError(
                            f'No PPS input event received on EXTTS channels {expired} '
                            f'for {event_timeout:g} seconds')
                    if now >= next_mapping_check:
                        for gpio, channel in inputs:
                            self._verify_duplex_mappings(gpio, output_gpio,
                                channel, tuple(item for item in inputs
                                               if item != (gpio, channel)))
                        next_mapping_check = now + self.output_monitor_interval
                    wait = min(0.2, *(event_timeout -
                        (now - last_event[channel]) for channel in channels))
                    try:
                        channel, line = lines.get(timeout=max(0.001, wait))
                    except queue.Empty:
                        continue
                    if line is None:
                        children[channel].wait()
                        raise RuntimeError(
                            f'testptp stopped while EXTTS channel {channel} was active')
                    if line.startswith(ERROR_PREFIXES):
                        raise RuntimeError(
                            f'testptp channel {channel} reported an input error: {line}')
                    if line == 'external time stamp request okay':
                        acknowledged.add(channel)
                        continue
                    match = EVENT_PATTERN.match(line)
                    if not match:
                        continue
                    if channel not in acknowledged:
                        raise RuntimeError(
                            f'testptp channel {channel} returned an event before '
                            'acknowledging EXTTS setup')
                    event_channel, sec, nsec = match.groups()
                    if int(event_channel) != channel:
                        raise RuntimeError(
                            f'Unexpected PTP timestamp channel {event_channel}, '
                            f'expected {channel}')
                    event = {'channel': channel,
                             'timestamp_ns': int(sec) * 1_000_000_000 + int(nsec)}
                    counts[channel] += 1
                    last_event[channel] = time.monotonic()
                    if input_inactive:
                        print('PPS inputs resumed', file=sys.stderr, flush=True)
                        input_inactive = False
                    if event_callback:
                        event_callback(event)
            except KeyboardInterrupt:
                outcome = 'signal'
            except Exception as exc:
                outcome = exc
            finally:
                disable_errors = []
                for channel in channels:
                    if channel not in cleanup_state['active_channels']:
                        continue
                    try:
                        self.extts_disable_fn(channel)
                        cleanup_state['active_channels'].remove(channel)
                    except Exception as exc:
                        disable_errors.append(f'EXTTS{channel}: {exc}')
                for child in children.values():
                    if child.poll() is None:
                        child.terminate()
                        try:
                            child.wait(timeout=2)
                        except subprocess.TimeoutExpired:
                            child.kill()
                            child.wait()
                for reader in readers:
                    reader.join(timeout=2)
                for child in children.values():
                    if child.stdout:
                        child.stdout.close()
                if disable_errors:
                    raise RuntimeError(
                        'EXTTS disable failed; retaining all GPIO mappings and '
                        'PEROUT: ' + '; '.join(disable_errors))

            if isinstance(outcome, TimeoutError):
                if not input_inactive:
                    print(f'{outcome}; rearming input capture in '
                          f'{self.input_retry_delay:g} seconds',
                          file=sys.stderr, flush=True)
                    input_inactive = True
                try:
                    self.sleep(self.input_retry_delay)
                except KeyboardInterrupt:
                    return {'events_received': counts, 'stopped_by': 'signal'}
                continue
            if isinstance(outcome, Exception):
                raise outcome
            return {'events_received': counts, 'stopped_by': 'signal'}

    def duplex(self, input_gpio, output_gpio, *, input_channel=0,
               input_gpio2=None,
               edge='rising', events=4,
               period_ns=1_000_000_000, high_ns=10_000_000,
               phase_ns=0, timeout=None, event_timeout=5.0,
               event_callback=None):
        plan = self.plan_duplex(input_gpio, output_gpio,
            input_channel=input_channel, input_gpio2=input_gpio2, edge=edge,
            events=events, period_ns=period_ns, high_ns=high_ns,
            phase_ns=phase_ns, timeout=timeout, event_timeout=event_timeout)
        inputs = [(input_gpio, input_channel)]
        if input_gpio2 is not None:
            inputs.append((input_gpio2, 1))
        output_mapping_attempted = False
        input_mappings_attempted = []
        output_request_attempted = False
        output_stopped = False
        cleanup_errors = []
        cleanup_state = {'active_channels': set()}
        try:
            output_mapping_attempted = True
            self._track_mapping(output_gpio, 2, 0)
            self._command('-L', f'{output_gpio},2', timeout=10,
                          acknowledgments=('set pin function okay',))
            for gpio, channel in inputs:
                input_mappings_attempted.append((gpio, channel))
                self._track_mapping(gpio, 1, channel)
                self._command('-L', f'{gpio},1', index=channel, timeout=10,
                              acknowledgments=('set pin function okay',))
            self._verify_duplex_mappings(input_gpio, output_gpio,
                input_channel, inputs[1:])
            output_request_attempted = True
            output_response = self._command('-p', str(period_ns), '-H',
                str(phase_ns), '-w', str(high_ns), timeout=10,
                acknowledgments=('periodic output request okay',))
            if events == 0:
                capture = self._capture_duplex_continuous(
                    inputs, output_gpio, edge, event_timeout=event_timeout,
                    event_callback=event_callback, cleanup_state=cleanup_state)
                events_by_channel = capture['events_received']
                records = []
            elif len(inputs) == 1:
                records = self._capture_duplex_events(
                    input_gpio, output_gpio, edge, events,
                    input_channel=input_channel, timeout=plan['timeout_s'],
                    event_timeout=event_timeout, event_callback=event_callback,
                    cleanup_state=cleanup_state)
                events_by_channel = {input_channel: records}
            else:
                events_by_channel = {}
                with ThreadPoolExecutor(max_workers=len(inputs)) as executor:
                    futures = {}
                    for gpio, channel in inputs:
                        other_inputs = tuple(item for item in inputs
                                             if item != (gpio, channel))
                        futures[channel] = executor.submit(
                            self._capture_duplex_events, gpio, output_gpio,
                            edge, events, input_channel=channel,
                            timeout=plan['timeout_s'],
                            event_timeout=event_timeout,
                            event_callback=event_callback,
                            other_inputs=other_inputs,
                            cleanup_state=cleanup_state)
                    for channel, future in futures.items():
                        events_by_channel[channel] = future.result()
                records = sorted(
                    [dict(event, gpio=gpio)
                     for gpio, channel in inputs
                     for event in events_by_channel[channel]],
                    key=lambda event: event['timestamp_ns'])
            return {'passed': True, 'mode': 'duplex',
                    'input_gpio': input_gpio, 'output_gpio': output_gpio,
                    'input_channel': input_channel,
                    'input_gpio2': input_gpio2,
                    'events_by_channel': events_by_channel,
                    'edge': edge, 'events': records,
                    'period_ns': period_ns, 'high_ns': high_ns,
                    'phase_ns': phase_ns, 'testptp_output': output_response,
                    'continuous': events == 0,
                    'stopped_by': capture.get('stopped_by') if events == 0 else None}
        finally:
            inputs_stopped = not cleanup_state['active_channels']
            if inputs_stopped:
                for gpio, channel in reversed(input_mappings_attempted):
                    try:
                        self._command('-L', f'{gpio},0', index=channel,
                                      timeout=10,
                                      acknowledgments=('set pin function okay',))
                        self._forget_mapping(gpio)
                    except Exception as exc:
                        cleanup_errors.append(f'EXTTS{channel} unmap failed: {exc}')
            elif input_mappings_attempted:
                cleanup_errors.append(
                    'EXTTS disable failed; retaining all GPIO mappings and PEROUT')
            if output_request_attempted and inputs_stopped:
                try:
                    self._command('-p', '0', timeout=10,
                                  acknowledgments=('periodic output request okay',))
                    output_stopped = True
                except Exception as exc:
                    cleanup_errors.append(f'PEROUT disable failed: {exc}')
            if output_mapping_attempted and (not output_request_attempted or
                                              (inputs_stopped and output_stopped)):
                try:
                    self._command('-L', f'{output_gpio},0', timeout=10,
                                  acknowledgments=('set pin function okay',))
                    self._forget_mapping(output_gpio)
                except Exception as exc:
                    cleanup_errors.append(f'PEROUT unmap failed: {exc}')
            if cleanup_errors:
                raise RuntimeError('; '.join(cleanup_errors))

    def output(self, gpio, *, period_ns=1_000_000_000,
               high_ns=10_000_000, phase_ns=0, duration=None):
        plan = self.plan('output', gpio, period_ns=period_ns,
                         high_ns=high_ns, phase_ns=phase_ns, duration=duration)
        mapped = False
        start_attempted = False
        cleanup_error = None
        started_at = None
        stopped_by = 'duration'
        try:
            self._track_mapping(gpio, 2, 0)
            self._command('-L', f'{gpio},2', timeout=10,
                          acknowledgments=('set pin function okay',))
            mapped = True
            start_attempted = True
            output = self._command('-p', str(period_ns),
                '-H', str(phase_ns), '-w', str(high_ns), timeout=10,
                acknowledgments=('periodic output request okay',))
            started_at = self.monotonic()
            deadline = None if duration is None else started_at + duration
            next_monitor = started_at
            try:
                while deadline is None or self.monotonic() < deadline:
                    now = self.monotonic()
                    if now >= next_monitor:
                        self._verify_output_mapping(gpio)
                        next_monitor = now + self.output_monitor_interval
                    wait = min(0.25, max(0, next_monitor - self.monotonic()))
                    if deadline is not None:
                        wait = min(wait, max(0, deadline - self.monotonic()))
                    self.sleep(wait)
            except KeyboardInterrupt:
                stopped_by = 'interrupt'
        finally:
            if start_attempted:
                try:
                    self._command('-p', '0', timeout=10,
                                  acknowledgments=('periodic output request okay',))
                except Exception as exc:
                    cleanup_error = f'PEROUT disable failed: {exc}'
            if mapped and cleanup_error is None:
                try:
                    self._command('-L', f'{gpio},0', timeout=10,
                                  acknowledgments=('set pin function okay',))
                    self._forget_mapping(gpio)
                except Exception as exc:
                    cleanup_error = f'PEROUT stopped but pin unmap failed: {exc}'
        if cleanup_error:
            raise RuntimeError(cleanup_error)
        return {'passed': True, 'mode': 'output', 'gpio': gpio,
                'period_ns': period_ns, 'high_ns': high_ns,
                'phase_ns': phase_ns, 'duration_s': duration,
                'stopped_by': stopped_by, 'testptp_output': output}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--interface', default='eth0')
    parser.add_argument('--testptp', default='testptp')
    parser.add_argument('--mode', choices=('input', 'output', 'duplex'), required=True)
    parser.add_argument('--gpio', type=int,
                        help='single-direction GPIO, RP1/BCM number 0..27')
    parser.add_argument('--input-gpio', type=int,
                        help='duplex EXTTS GPIO, RP1/BCM number 0..27')
    parser.add_argument('--input-gpio2', type=_optional_gpio,
                        help='optional second duplex EXTTS GPIO on channel 1')
    parser.add_argument('--output-gpio', type=int,
                        help='duplex PEROUT GPIO, RP1/BCM number 0..27')
    parser.add_argument('--input-channel', type=int, default=0,
                        help='PTP EXTTS channel, 0 or 1')
    parser.add_argument('--edge', choices=('rising', 'falling'), default='rising')
    parser.add_argument('--events', type=int, default=4,
                        help='input event count; use 0 for continuous service capture')
    parser.add_argument('--timeout', type=float,
                        help='input capture timeout, default is max(5 s, 3 s/event)')
    parser.add_argument('--event-timeout', type=float, default=5.0,
                        help='continuous capture inactivity timeout in seconds')
    parser.add_argument('--period-ns', type=int, default=1_000_000_000)
    parser.add_argument('--high-ns', type=int, default=10_000_000)
    parser.add_argument('--phase-ns', type=int, default=0)
    parser.add_argument('--duration', type=float,
                        help='output run time; omit to run until Ctrl-C')
    parser.add_argument('--ownership-file',
                        help='record mappings owned by this service for crash cleanup')
    parser.add_argument('--cleanup', action='store_true',
                        help='recover only mappings recorded for this service')
    parser.add_argument('--execute', action='store_true',
                        help='apply the plan; without this flag only inspect and print')
    parser.add_argument('--wait-for-phc', action='store_true',
                        help='wait for a supported RP1 PHC before starting')
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, _handle_sigterm)
    try:
        if args.timeout is not None and args.timeout <= 0:
            raise ValueError('timeout must be positive')
        if args.event_timeout <= 0:
            raise ValueError('event-timeout must be positive')
        if args.duration is not None and args.duration <= 0:
            raise ValueError('duration must be positive')
        if args.mode == 'duplex':
            if args.gpio is not None or args.input_gpio is None or args.output_gpio is None:
                raise ValueError('duplex mode requires --input-gpio and --output-gpio')
            if args.duration is not None:
                raise ValueError('duplex mode runs until the event count or a signal stops it')
        elif (args.gpio is None or args.input_gpio is not None or
              args.input_gpio2 is not None or args.output_gpio is not None):
            raise ValueError('input/output mode requires --gpio only')
        if args.cleanup and (args.execute or not args.ownership_file):
            raise ValueError('--cleanup requires --ownership-file and cannot use --execute')
        if args.cleanup and args.mode == 'duplex':
            if args.input_gpio is None or args.output_gpio is None or args.gpio is not None:
                raise ValueError('duplex cleanup requires --input-gpio and --output-gpio')
            expected = [{'gpio': args.input_gpio, 'function': 1, 'channel': 0},
                        {'gpio': args.output_gpio, 'function': 2, 'channel': 0}]
            if args.input_gpio2 is not None:
                expected.append({'gpio': args.input_gpio2,
                                 'function': 1, 'channel': 1})
        elif args.cleanup:
            if args.gpio is None or args.input_gpio is not None or args.output_gpio is not None:
                raise ValueError('single-direction cleanup requires --gpio only')
            expected = [{'gpio': args.gpio,
                         'function': 1 if args.mode == 'input' else 2,
                         'channel': args.input_channel if args.mode == 'input' else 0}]
        else:
            expected = None
        pps = RP1PPS(interface=args.interface, testptp=args.testptp,
                     ownership_file=args.ownership_file)
        if args.cleanup:
            print(json.dumps(pps.cleanup_owned_mappings(expected), indent=2))
            return
        while True:
            try:
                preflight = pps.preflight()
                break
            except PHCUnavailable as exc:
                if not args.wait_for_phc:
                    raise
                print(f'Waiting for a supported RP1 PHC: {exc}', file=sys.stderr,
                      flush=True)
                time.sleep(5)
        if args.mode == 'duplex':
            plan = pps.plan_duplex(args.input_gpio, args.output_gpio,
                input_channel=args.input_channel, input_gpio2=args.input_gpio2,
                edge=args.edge,
                events=args.events, period_ns=args.period_ns,
                high_ns=args.high_ns, phase_ns=args.phase_ns,
                timeout=args.timeout, event_timeout=args.event_timeout)
        else:
            plan = pps.plan(args.mode, args.gpio, edge=args.edge,
                channel=args.input_channel if args.mode == 'input' else 0,
                events=args.events, period_ns=args.period_ns,
                high_ns=args.high_ns, phase_ns=args.phase_ns,
                duration=args.duration, timeout=args.timeout,
                event_timeout=args.event_timeout)
        result = {'preflight': preflight, 'plan': plan}
        if args.execute:
            result['result'] = (pps.capture(args.gpio, edge=args.edge,
                channel=args.input_channel,
                events=args.events, timeout=args.timeout,
                event_timeout=args.event_timeout,
                event_callback=lambda event: print(
                    json.dumps({'event': event}), flush=True)) if args.mode == 'input'
                else pps.output(args.gpio, period_ns=args.period_ns,
                    high_ns=args.high_ns, phase_ns=args.phase_ns,
                    duration=args.duration) if args.mode == 'output'
                else pps.duplex(args.input_gpio, args.output_gpio,
                    input_channel=args.input_channel, edge=args.edge,
                    input_gpio2=args.input_gpio2,
                    events=args.events,
                    period_ns=args.period_ns, high_ns=args.high_ns,
                    phase_ns=args.phase_ns, timeout=args.timeout,
                    event_timeout=args.event_timeout,
                    event_callback=lambda event: print(
                        json.dumps({'event': event}), flush=True)))
        print(json.dumps(result, indent=2))
    except (OSError, RuntimeError, TimeoutError, ValueError) as exc:
        parser.exit(1, f'{exc}\n')
    except KeyboardInterrupt:
        parser.exit(130, 'interrupted\n')


def _handle_sigterm(_signum, _frame):
    """Turn service stop into an exception so active PPS is cleaned up."""
    raise KeyboardInterrupt


if __name__ == '__main__':
    main()
