#!/usr/bin/env python3
"""Offline tests for guarded testptp GPIO control."""
import importlib.util
import io
import tempfile
import threading
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
SPEC = importlib.util.spec_from_file_location('rp1_pps', ROOT / 'rp1_pps.py')
rp1 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(rp1)


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakePopen:
    def __init__(self, argv, output='', *, timeout=False, returncode=0):
        self.argv = argv
        self.output = output
        self.timeout = timeout
        self.returncode = returncode
        self.terminated = False
        self.killed = False

    def communicate(self, timeout=None):
        if self.timeout and not self.terminated:
            raise subprocess.TimeoutExpired(self.argv, timeout)
        return self.output, ''

    def terminate(self):
        self.terminated = True
        self.returncode = -15

    def kill(self):
        self.killed = True
        self.terminated = True
        self.returncode = -9

    def wait(self, timeout=None):
        return self.returncode


class FakeStream:
    def __init__(self, events, channel=0):
        self.events = events
        self.channel = channel
        self.stopped = threading.Event()

    def __iter__(self):
        yield 'external time stamp request okay\n'
        for index in range(self.events):
            yield f'event index {self.channel} at {100 + index}.000000123\n'
        self.stopped.wait()

    def close(self):
        self.stopped.set()


class FakeStreamPopen:
    def __init__(self, argv, events):
        self.argv = argv
        self.stdout = FakeStream(events, int(argv[4]))
        self.returncode = None

    def poll(self):
        return self.returncode

    def terminate(self):
        self.returncode = -15
        self.stdout.stopped.set()

    def kill(self):
        self.returncode = -9
        self.stdout.stopped.set()

    def wait(self, timeout=None):
        return self.returncode


class FakeFiniteStreamPopen:
    def __init__(self, argv, output):
        self.argv = argv
        self.stdout = io.StringIO(output)
        self.returncode = 0

    def poll(self):
        return self.returncode

    def wait(self, timeout=None):
        return self.returncode

    def terminate(self):
        self.returncode = -15

    def kill(self):
        self.returncode = -9


class RP1PPSTests(unittest.TestCase):
    def test_duplex_unit_runs_testptp_with_two_input_channels_and_output(self):
        unit = (ROOT / 'rp1-pps-duplex.service.in').read_text()
        self.assertIn('--mode duplex', unit)
        self.assertIn('--input-gpio ${PPS_INPUT_GPIO}', unit)
        self.assertIn('--input-gpio2 ${PPS_INPUT_GPIO2}', unit)
        self.assertIn('--output-gpio ${PPS_OUTPUT_GPIO}', unit)
        self.assertIn('--ownership-file /var/lib/rp1-ptp-pps/owned.json --cleanup', unit)
        self.assertIn('--execute --wait-for-phc', unit)
        self.assertIn('@CMAKE_INSTALL_FULL_LIBEXECDIR@/rp1-ptp-pps/rp1_pps.py', unit)
        self.assertIn('Conflicts=rp1-pps.service', unit)

    def setUp(self):
        self.calls = []
        self.pin_functions = {gpio: 0 for gpio in range(28)}
        self.pin_channels = {gpio: 0 for gpio in range(28)}
        self.fail_output_stop = False
        self.capture_error = None
        self.capture_timeout = False
        self.fail_extts_disable = False
        self.disable_calls = []
        self.lose_mapping_gpio = None
        self.interrupt_on_sleep = False
        self.clock = FakeClock()
        self.tempdir = tempfile.TemporaryDirectory()
        self.ownership_file = Path(self.tempdir.name) / 'owned.json'

        def sleep(seconds):
            self.clock.sleep(seconds)
            if self.lose_mapping_gpio is not None:
                self.pin_functions[self.lose_mapping_gpio] = 0
                self.lose_mapping_gpio = None
            if self.interrupt_on_sleep:
                self.interrupt_on_sleep = False
                raise KeyboardInterrupt

        def inventory(_interface):
            return {'driver': 'macb', 'clocks': [{
                'name': 'gem-ptp-timer', 'device': '/dev/ptp7',
                'parent': '/sys/devices/platform/rp1/net/eth0',
                'device_number': [248, 7], 'rp1_mac': True,
                'capabilities': {'extts': 2, 'perout': 1, 'pins': 28}}]}

        def disable_extts(channel):
            self.disable_calls.append(channel)
            self.calls.append(['ioctl-disable', str(channel)])
            if self.fail_extts_disable:
                raise OSError('injected PTP_EXTTS_REQUEST disable failure')

        def popen(argv, **_kwargs):
            self.calls.append(list(argv))
            options = argv[5:]
            if options == ['-c']:
                output = ('capabilities:\n  2 external time stamp channels\n'
                          '  1 programmable periodic signals\n  28 programmable pins\n')
            elif options == ['-l']:
                output = ''.join(f'name GPIO{gpio} index {gpio} func '
                    f'{self.pin_functions[gpio]} chan {self.pin_channels[gpio]}\n'
                    for gpio in range(28))
            elif options and options[0] == '-L':
                gpio, function = map(int, options[1].split(','))
                channel = int(argv[4])
                if function:
                    self.pin_functions[gpio] = function
                    self.pin_channels[gpio] = channel
                else:
                    self.pin_functions[gpio] = 0
                    self.pin_channels[gpio] = 0
                output = 'set pin function okay\n'
            elif options and options[0] == '-E':
                output = self.capture_error or 'external time stamp request okay\n'
                if not self.capture_error:
                    count = int(options[options.index('-e') + 1])
                    output += ''.join(f'event index {argv[4]} at {1+i}.000000123\n'
                                      for i in range(count))
            elif options and options[0] == '-p':
                if options[1] == '0' and self.fail_output_stop:
                    output = 'PTP_PEROUT_REQUEST: Connection timed out\n'
                else:
                    output = 'periodic output request okay\n'
            else:
                raise AssertionError(f'Unexpected testptp options: {options!r}')
            return FakePopen(argv, output, timeout=(options and options[0] == '-E'
                and self.capture_timeout))

        self.tool = rp1.RP1PPS(interface='eth0', testptp='/opt/testptp',
            inventory_fn=inventory, monotonic=self.clock.monotonic,
            sleep=sleep, popen_factory=popen,
            extts_disable_fn=disable_extts)

    def tearDown(self):
        self.tempdir.cleanup()

    def test_standard_extts_disable_ioctl_clears_the_selected_channel(self):
        tool = rp1.RP1PPS()
        tool.clock = {'device': '/dev/ptp-test'}
        with patch.object(rp1.os, 'open', return_value=17) as open_device, \
                patch.object(rp1.os, 'close') as close_device, \
                patch.object(rp1.fcntl, 'ioctl') as ioctl:
            tool._disable_extts_request(1)
        open_device.assert_called_once_with('/dev/ptp-test',
            rp1.os.O_RDWR | rp1.os.O_CLOEXEC)
        ioctl.assert_called_once()
        fd, request_number, payload, mutate = ioctl.call_args.args
        self.assertEqual(fd, 17)
        self.assertEqual(request_number, rp1.PTP_EXTTS_REQUEST_IOCTL)
        self.assertEqual(bytes(payload), b'\x01\x00\x00\x00' + b'\x00' * 12)
        self.assertTrue(mutate)
        close_device.assert_called_once_with(17)

    def test_missing_phc_is_retryable_but_occupied_pin_map_is_not(self):
        unavailable = rp1.RP1PPS(inventory_fn=lambda _interface: {
            'driver': 'macb', 'clocks': []})
        with self.assertRaises(rp1.PHCUnavailable):
            unavailable.preflight()

        self.pin_functions[7] = 1
        with self.assertRaisesRegex(RuntimeError, 'Existing PTP pin mappings'):
            self.tool.preflight()

    def test_owned_duplex_cleanup_disables_both_channels_and_perout(self):
        self.tool.preflight()
        self.tool.ownership_file = self.ownership_file
        self.tool.boot_id_fn = lambda: 'test-boot'
        rows = [{'gpio': 18, 'function': 1, 'channel': 0},
                {'gpio': 23, 'function': 2, 'channel': 0},
                {'gpio': 24, 'function': 1, 'channel': 1}]
        for row in rows:
            self.pin_functions[row['gpio']] = row['function']
            self.pin_channels[row['gpio']] = row['channel']
            self.tool._track_mapping(**row)

        result = self.tool.cleanup_owned_mappings(rows)

        self.assertTrue(result['cleaned'])
        self.assertEqual(self.disable_calls, [0, 1])
        self.assertIn(['-p', '0'], [call[5:7] for call in self.calls])
        self.assertEqual([self.pin_functions[gpio] for gpio in (18, 23, 24)],
                         [0, 0, 0])
        self.assertFalse(self.ownership_file.exists())

    def test_owned_cleanup_refuses_to_clear_a_different_configuration(self):
        self.tool.preflight()
        self.tool.ownership_file = self.ownership_file
        self.tool.boot_id_fn = lambda: 'test-boot'
        self.pin_functions[18] = 1
        self.tool._track_mapping(18, 1, 0)
        command_count = len(self.calls)

        result = self.tool.cleanup_owned_mappings([
            {'gpio': 19, 'function': 1, 'channel': 0}])

        self.assertFalse(result['cleaned'])
        self.assertEqual(result['reason'], 'ownership record mismatch')
        self.assertEqual(len(self.calls), command_count)
        self.assertEqual(self.pin_functions[18], 1)
        self.assertTrue(self.ownership_file.exists())

    def test_capture_uses_selected_gpio_and_unmaps_after_success(self):
        self.tool.preflight()
        result = self.tool.capture(12, edge='falling', events=3)
        self.assertTrue(result['passed'])
        self.assertEqual([r['timestamp_ns'] for r in result['events']], [
            1_000_000_123, 2_000_000_123, 3_000_000_123])
        self.assertIn(['-L', '12,1'], [call[5:7] for call in self.calls])
        self.assertIn(['-E', '2', '-e', '3'], [call[5:9] for call in self.calls])
        self.assertEqual(self.pin_functions[12], 0)

    def test_input_only_mode_rejects_channel_one_without_perout(self):
        self.tool.preflight()
        before = len(self.calls)
        with self.assertRaisesRegex(ValueError, 'requires active PEROUT'):
            self.tool.capture(13, channel=1, events=2)
        self.assertEqual(len(self.calls), before)

    def test_capture_error_cleans_selected_mapping(self):
        self.tool.preflight()
        self.capture_error = 'external time stamp request okay\nread: Input/output error\n'
        with self.assertRaisesRegex(RuntimeError, 'ioctl/read error'):
            self.tool.capture(18)
        self.assertEqual(self.pin_functions[18], 0)
        self.assertEqual(self.calls[-1][5:7], ['-L', '18,0'])

    def test_capture_timeout_terminates_testptp_then_unmaps(self):
        self.tool.preflight()
        self.capture_timeout = True
        with self.assertRaises(TimeoutError):
            self.tool.capture(23, timeout=1)
        self.assertEqual(self.pin_functions[23], 0)
        self.assertEqual(self.calls[-1][5:7], ['-L', '23,0'])

    def test_continuous_input_streams_events_and_stops_cleanly(self):
        self.tool.preflight()
        children = []

        def popen(argv, **kwargs):
            self.calls.append(list(argv))
            if argv[-2:] == ['-e', '2147483647']:
                child = FakeStreamPopen(argv, events=2)
                children.append(child)
                return child
            return self.tool_popen(argv, **kwargs)

        self.tool_popen = self.tool.popen_factory
        self.tool.popen_factory = popen
        observed = []

        def stop_after_two(event):
            observed.append(event)
            if len(observed) == 2:
                raise KeyboardInterrupt

        result = self.tool.capture(10, events=0, event_timeout=1,
                                   event_callback=stop_after_two)
        self.assertEqual(result['events_received'], 2)
        self.assertEqual(result['stopped_by'], 'signal')
        self.assertEqual([row['timestamp_ns'] for row in observed], [
            100_000_000_123, 101_000_000_123])
        self.assertEqual(children[0].returncode, -15)
        self.assertEqual(self.pin_functions[10], 0)
        self.assertIn(['-E', '1', '-e', '2147483647'],
                      [call[5:] for call in self.calls])

    def test_continuous_input_rearms_after_inactivity_without_unmapping(self):
        self.tool.preflight()
        self.interrupt_on_sleep = True
        children = []

        def popen(argv, **kwargs):
            self.calls.append(list(argv))
            if argv[-2:] == ['-e', '2147483647']:
                child = FakeStreamPopen(argv, events=0)
                children.append(child)
                return child
            return self.tool_popen(argv, **kwargs)

        self.tool_popen = self.tool.popen_factory
        self.tool.popen_factory = popen
        result = self.tool.capture(11, events=0, event_timeout=0.05)
        self.assertEqual(result['stopped_by'], 'signal')
        self.assertEqual(len(children), 1)
        self.assertEqual(children[0].returncode, -15)
        self.assertEqual(self.disable_calls, [0])
        self.assertEqual(self.pin_functions[11], 0)
        self.assertEqual(self.calls[-1][5:7], ['-L', '11,0'])

    def test_continuous_input_recovers_and_delivers_events_after_rearm(self):
        self.tool.preflight()
        children = []

        def popen(argv, **kwargs):
            self.calls.append(list(argv))
            if argv[-2:] == ['-e', '2147483647']:
                child = FakeStreamPopen(argv, events=0 if not children else 2)
                children.append(child)
                return child
            return self.tool_popen(argv, **kwargs)

        self.tool_popen = self.tool.popen_factory
        self.tool.popen_factory = popen
        observed = []

        def stop_after_recovery(event):
            observed.append(event)
            if len(observed) == 2:
                raise KeyboardInterrupt

        result = self.tool.capture(11, events=0, event_timeout=0.05,
                                   event_callback=stop_after_recovery)
        self.assertEqual(result['events_received'], 2)
        self.assertEqual(len(children), 2)
        self.assertEqual(self.disable_calls, [0, 0])
        self.assertEqual(self.pin_functions[11], 0)
        self.assertEqual([row['timestamp_ns'] for row in observed], [
            100_000_000_123, 101_000_000_123])

    def test_continuous_input_disable_failure_preserves_gpio_mapping(self):
        self.tool.preflight()
        self.fail_extts_disable = True

        def popen(argv, **kwargs):
            self.calls.append(list(argv))
            if argv[-2:] == ['-e', '2147483647']:
                return FakeStreamPopen(argv, events=0)
            return self.tool_popen(argv, **kwargs)

        self.tool_popen = self.tool.popen_factory
        self.tool.popen_factory = popen
        with self.assertRaisesRegex(RuntimeError, 'disable failed'):
            self.tool.capture(11, events=0, event_timeout=0.05)
        self.assertEqual(self.pin_functions[11], 1)
        self.assertEqual(self.disable_calls, [0])

    def test_output_runs_then_disables_before_unmapping(self):
        self.tool.preflight()
        result = self.tool.output(16, period_ns=2_000_000_000,
                                  high_ns=20_000_000, phase_ns=10,
                                  duration=0.5)
        self.assertTrue(result['passed'])
        options = [call[5:] for call in self.calls]
        self.assertIn(['-L', '16,2'], options)
        self.assertIn(['-p', '2000000000', '-H', '10', '-w', '20000000'], options)
        self.assertLess(options.index(['-p', '0']), options.index(['-L', '16,0']))
        self.assertEqual(self.pin_functions[16], 0)

    def test_output_stop_failure_retains_pin_mapping(self):
        self.tool.preflight()
        self.fail_output_stop = True
        with self.assertRaisesRegex(RuntimeError, 'PEROUT disable failed'):
            self.tool.output(6, duration=0.1)
        self.assertEqual(self.pin_functions[6], 2)
        self.assertNotIn(['-L', '6,0'], [call[5:7] for call in self.calls])

    def test_output_aborts_and_cleans_up_if_mapping_disappears(self):
        self.tool.preflight()
        self.lose_mapping_gpio = 6
        with self.assertRaisesRegex(RuntimeError, 'lost its PEROUT mapping'):
            self.tool.output(6, duration=5)
        options = [call[5:] for call in self.calls]
        self.assertIn(['-l'], options)
        self.assertLess(options.index(['-p', '0']), options.index(['-L', '6,0']))
        self.assertEqual(self.pin_functions[6], 0)

    def duplex_stream_factory(self, output):
        old_factory = self.tool.popen_factory

        def popen(argv, **kwargs):
            if argv[5:7] == ['-E', '1'] or argv[5:7] == ['-E', '2']:
                self.calls.append(list(argv))
                channel = argv[4]
                indexed_output = output.replace('event index 0 at',
                                                f'event index {channel} at')
                return FakeFiniteStreamPopen(argv, indexed_output)
            return old_factory(argv, **kwargs)

        self.tool.popen_factory = popen

    def test_interrupt_stops_output_and_runs_cleanup(self):
        self.tool.preflight()
        self.interrupt_on_sleep = True
        result = self.tool.output(6, duration=10)
        self.assertEqual(result['stopped_by'], 'interrupt')
        options = [call[5:] for call in self.calls]
        self.assertLess(options.index(['-p', '0']), options.index(['-L', '6,0']))
        self.assertEqual(self.pin_functions[6], 0)

    def test_sigterm_handler_requests_graceful_unwind(self):
        with self.assertRaises(KeyboardInterrupt):
            rp1._handle_sigterm(15, None)

    def test_preflight_refuses_preexisting_mapping_before_any_write(self):
        self.pin_functions[7] = 1
        with self.assertRaisesRegex(RuntimeError, 'Existing PTP pin mappings'):
            self.tool.preflight()
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(self.pin_functions[7], 1)

    def test_plan_validates_gpio_and_timing_without_actuation(self):
        self.tool.preflight()
        with self.assertRaises(ValueError):
            self.tool.plan('input', 28)
        with self.assertRaisesRegex(ValueError, 'requires active PEROUT'):
            self.tool.plan('input', 18, channel=1)
        with self.assertRaises(ValueError):
            self.tool.plan('output', 18, period_ns=100, high_ns=100)
        plan = self.tool.plan('output', 23)
        self.assertIn('-L 23,2', plan['start'])
        self.assertIn('-p 0', plan['stop'])

    def test_plan_uses_testptp_long_count_for_continuous_input(self):
        self.tool.preflight()
        plan = self.tool.plan('input', 18, events=0)
        self.assertTrue(plan['continuous'])
        self.assertIn('-e 2147483647', plan['capture'])
        with self.assertRaisesRegex(ValueError, 'events must be zero'):
            self.tool.plan('input', 18, events=-1)

    def test_duplex_plan_supports_continuous_two_channel_capture(self):
        self.tool.preflight()
        plan = self.tool.plan_duplex(18, 23, input_gpio2=19, events=0)
        self.assertTrue(plan['continuous'])
        self.assertIn('-e 2147483647', plan['capture_input'])
        self.assertIn('-e 2147483647', plan['capture_input2'])
        self.assertIsNone(rp1._optional_gpio('none'))
        self.assertEqual(rp1._optional_gpio('24'), 24)

    def test_duplex_starts_perout_then_captures_extts_and_cleans_in_order(self):
        self.tool.preflight()
        self.duplex_stream_factory(
            'external time stamp request okay\n'
            'event index 0 at 100.000000123\n'
            'event index 0 at 101.000000456\n')
        result = self.tool.duplex(18, 23, events=2)
        self.assertTrue(result['passed'])
        self.assertEqual([event['timestamp_ns'] for event in result['events']], [
            100_000_000_123, 101_000_000_456])
        options = [call[5:] for call in self.calls]
        self.assertLess(options.index(['-L', '23,2']),
                        options.index(['-L', '18,1']))
        self.assertLess(options.index(['-L', '18,1']),
                        options.index(['-p', '1000000000', '-H', '0', '-w', '10000000']))
        self.assertLess(options.index(['-p', '1000000000', '-H', '0', '-w', '10000000']),
                        options.index(['-E', '1', '-e', '2']))
        self.assertLess(options.index(['-L', '18,0']), options.index(['-p', '0']))
        self.assertLess(options.index(['-L', '18,0']), options.index(['-L', '23,0']))
        self.assertEqual(self.pin_functions[18], 0)
        self.assertEqual(self.pin_functions[23], 0)

    def test_duplex_channel_one_maps_and_checks_indexed_events(self):
        self.tool.preflight()
        self.duplex_stream_factory(
            'external time stamp request okay\n'
            'event index 1 at 100.000000123\n')
        result = self.tool.duplex(18, 23, input_channel=1, events=1)
        self.assertTrue(result['passed'])
        self.assertEqual(result['input_channel'], 1)
        self.assertEqual(result['events'][0]['channel'], 1)
        input_map = next(call for call in self.calls if call[5:7] == ['-L', '18,1'])
        self.assertEqual(input_map[4], '1')
        self.assertEqual(self.pin_functions[18], 0)

    def test_duplex_captures_two_independent_input_channels(self):
        self.tool.preflight()
        self.duplex_stream_factory(
            'external time stamp request okay\n'
            'event index 0 at 100.000000123\n')
        result = self.tool.duplex(18, 23, input_gpio2=19, events=1)
        self.assertTrue(result['passed'])
        self.assertEqual(set(result['events_by_channel']), {0, 1})
        self.assertEqual(result['events_by_channel'][0][0]['channel'], 0)
        self.assertEqual(result['events_by_channel'][1][0]['channel'], 1)
        self.assertEqual({self.pin_functions[18], self.pin_functions[19],
                          self.pin_functions[23]}, {0})
        mapped = [call for call in self.calls
                  if len(call) > 6 and call[5] == '-L' and call[6].endswith(',1')]
        self.assertEqual({(call[6].split(',')[0], call[4]) for call in mapped},
                         {('18', '0'), ('19', '1')})

    def test_continuous_duplex_supervises_two_readers_and_cleans_in_order(self):
        self.tool.preflight()
        children = []
        old_factory = self.tool.popen_factory

        def popen(argv, **kwargs):
            if argv[5:7] in (['-E', '1'], ['-E', '2']):
                child = FakeStreamPopen(argv, events=1)
                children.append(child)
                return child
            return old_factory(argv, **kwargs)

        self.tool.popen_factory = popen
        observed = []

        def stop_after_both(event):
            observed.append(event)
            if len(observed) == 2:
                raise KeyboardInterrupt

        result = self.tool.duplex(18, 23, input_gpio2=19, events=0,
                                  event_timeout=1,
                                  event_callback=stop_after_both)
        self.assertTrue(result['continuous'])
        self.assertEqual(result['stopped_by'], 'signal')
        self.assertEqual({row['channel'] for row in observed}, {0, 1})
        self.assertEqual(self.disable_calls, [0, 1])
        self.assertEqual(len(children), 2)
        self.assertTrue(all(child.returncode == -15 for child in children))
        self.assertEqual({self.pin_functions[18], self.pin_functions[19],
                          self.pin_functions[23]}, {0})
        last_disable = max(i for i, call in enumerate(self.calls)
                           if call and call[0] == 'ioctl-disable')
        unmap_input = next(i for i, call in enumerate(self.calls)
                           if call[5:7] == ['-L', '19,0'])
        self.assertLess(last_disable, unmap_input)
        options = [call[5:] for call in self.calls]
        self.assertLess(options.index(['-L', '19,0']), options.index(['-p', '0']))

    def test_continuous_duplex_rearms_both_inputs_without_stopping_perout(self):
        self.tool.preflight()
        children = []
        old_factory = self.tool.popen_factory

        def popen(argv, **kwargs):
            if argv[5:7] in (['-E', '1'], ['-E', '2']):
                round_events = 0 if len(children) < 2 else 1
                child = FakeStreamPopen(argv, events=round_events)
                children.append(child)
                return child
            return old_factory(argv, **kwargs)

        self.tool.popen_factory = popen
        observed = []

        def stop_after_recovery(event):
            observed.append(event)
            if len(observed) == 2:
                raise KeyboardInterrupt

        result = self.tool.duplex(18, 23, input_gpio2=19, events=0,
            event_timeout=0.05, event_callback=stop_after_recovery)
        self.assertEqual(result['stopped_by'], 'signal')
        self.assertEqual(len(children), 4)
        self.assertEqual({row['channel'] for row in observed}, {0, 1})
        self.assertEqual(self.disable_calls, [0, 1, 0, 1])
        self.assertEqual(sum(call[5:7] == ['-p', '0'] for call in self.calls), 1)
        self.assertEqual({self.pin_functions[18], self.pin_functions[19],
                          self.pin_functions[23]}, {0})

    def test_continuous_duplex_disable_failure_preserves_all_pin_mappings(self):
        self.tool.preflight()
        self.fail_extts_disable = True
        old_factory = self.tool.popen_factory

        def popen(argv, **kwargs):
            if argv[5:7] in (['-E', '1'], ['-E', '2']):
                return FakeStreamPopen(argv, events=0)
            return old_factory(argv, **kwargs)

        self.tool.popen_factory = popen
        with self.assertRaisesRegex(RuntimeError, 'retaining all GPIO mappings'):
            self.tool.duplex(18, 23, input_gpio2=19, events=0,
                             event_timeout=0.05)
        self.assertEqual(self.pin_functions[18], 1)
        self.assertEqual(self.pin_functions[19], 1)
        self.assertEqual(self.pin_functions[23], 2)
        self.assertNotIn(['-p', '0'], [call[5:] for call in self.calls])

    def test_duplex_rejects_shared_gpio_before_mapping(self):
        self.tool.preflight()
        before = len(self.calls)
        with self.assertRaisesRegex(ValueError, 'different GPIO'):
            self.tool.plan_duplex(18, 18)
        with self.assertRaisesRegex(ValueError, 'different GPIO'):
            self.tool.plan_duplex(18, 23, input_gpio2=18)
        with self.assertRaisesRegex(ValueError, 'rising edges only'):
            self.tool.plan_duplex(18, 23, edge='falling')
        self.assertEqual(len(self.calls), before)

    def test_duplex_extts_rejection_stops_output_and_unmaps_both_pins(self):
        self.tool.preflight()
        self.duplex_stream_factory('PTP_EXTTS_REQUEST: Device or resource busy\n')
        with self.assertRaisesRegex(RuntimeError, 'input error'):
            self.tool.duplex(18, 23, events=2)
        options = [call[5:] for call in self.calls]
        self.assertLess(options.index(['-L', '18,0']), options.index(['-p', '0']))
        self.assertLess(options.index(['-L', '18,0']), options.index(['-L', '23,0']))
        self.assertEqual(self.pin_functions[18], 0)
        self.assertEqual(self.pin_functions[23], 0)

    def test_duplex_retains_output_mapping_if_perout_stop_fails(self):
        self.tool.preflight()
        self.duplex_stream_factory(
            'external time stamp request okay\n'
            'event index 0 at 100.000000123\n')
        self.fail_output_stop = True
        with self.assertRaisesRegex(RuntimeError, 'PEROUT disable failed'):
            self.tool.duplex(18, 23, events=1)
        self.assertEqual(self.pin_functions[18], 0)
        self.assertEqual(self.pin_functions[23], 2)


if __name__ == '__main__':
    unittest.main()
