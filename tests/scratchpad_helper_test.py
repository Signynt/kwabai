"""Isolated helper tests. No applications, windows or real processes are changed."""
import importlib.util
from pathlib import Path
import signal
import subprocess
import sys
import unittest
from unittest.mock import patch, Mock

SPEC = importlib.util.spec_from_file_location('scratchpad_helper', Path(__file__).resolve().parents[1] / 'examples/scratchpads.py')
helper = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = helper
SPEC.loader.exec_module(helper)


def app(name):
    return next(a for a in helper.APPS if a.name == name)


class HelperTests(unittest.TestCase):
    def test_dry_run_has_no_desktop_actions(self):
        result = subprocess.run([sys.executable, str(Path(helper.__file__)), '--dry-run'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(result.stdout.splitlines()), 5)
        self.assertIn('thunderbird-calendar.desktop', result.stdout)
        self.assertIn('reminders.desktop', result.stdout)

    def test_toggle_existing_uses_named_window(self):
        pads = helper.Scratchpads('kwabai')
        pads.ready = Mock()
        pads.rule = Mock()
        pads.windows = Mock(return_value=[{'app': 'ThunderCalendar', 'scratchpad': 'Calendar'}])
        pads.cli = Mock()
        pads.launch = Mock()
        pads.toggle(app('Calendar'))
        pads.cli.assert_called_once_with('-m', 'window', '--toggle', 'Calendar')
        pads.launch.assert_not_called()

    def test_toggle_closed_launches_and_sizes_without_hiding_new_window(self):
        pads = helper.Scratchpads('kwabai')
        pads.ready = Mock()
        pads.rule = Mock()
        pads.windows = Mock(return_value=[])
        pads.launch = Mock()
        window = {'app': 'elecwhat', 'id': '7', 'scratchpad': 'whatsapp', 'scratchpad-hidden': False}
        pads.wait_assigned = Mock(return_value=window)
        pads.grid = Mock()
        pads.cli = Mock()
        pads.toggle(app('whatsapp'))
        pads.launch.assert_called_once_with(app('whatsapp'))
        pads.grid.assert_called_once_with(window)
        pads.cli.assert_not_called()

    def test_wrong_app_name_conflict_does_not_toggle(self):
        pads = helper.Scratchpads('kwabai')
        with self.assertRaisesRegex(RuntimeError, 'another app'):
            pads.assigned(app('Calendar'), [{'app': 'kitty', 'scratchpad': 'Calendar'}])

    def test_restart_targets_pids_except_shared_browser_and_launches_all_first(self):
        pads = helper.Scratchpads('kwabai')
        pads.ready = Mock()
        pads.rule = Mock()
        original = [
            {'app': 'ThunderCalendar', 'uuid': 'calendar'},
            {'app': 'org.mozilla.Thunderbird', 'uuid': 'mail'},
            {'app': 'chrome-www.icloud.com__reminders_-Default', 'uuid': 'reminders'},
            {'app': 'firefox', 'uuid': 'unrelated'},
        ]
        pads.windows = Mock(side_effect=[original, []])
        pads.alive = Mock(return_value=False)
        events = []
        pads.launch = lambda a: events.append(('launch', a.name))
        pads.wait_assigned = lambda a: events.append(('wait', a.name)) or {'id': a.name, 'scratchpad-hidden': False}
        pads.grid = lambda w: events.append(('grid', w['id']))
        pads.cli = Mock()
        def checked(argv):
            if argv[1] == 'getwindowpid':
                self.assertNotIn('reminders', argv[2])
                return '990001' if argv[2] == '{calendar}' else '990002'
            self.assertEqual(argv, ['kdotool', 'windowclose', '{reminders}'])
            return ''
        with patch.object(helper, 'checked', side_effect=checked) as commands, patch.object(helper.os, 'kill') as kill, patch('builtins.print'):
            pads.restart()
        self.assertEqual({call.args for call in kill.call_args_list}, {(990001, signal.SIGTERM), (990002, signal.SIGTERM)})
        self.assertEqual(len(commands.call_args_list), 3)
        self.assertEqual([e[0] for e in events[:5]], ['launch'] * 5)
        self.assertEqual(pads.cli.call_count, 5)

    def test_grid_does_not_retry_uncertain_geometry_writes(self):
        pads = helper.Scratchpads('kwabai')
        uncertain = subprocess.CompletedProcess([], 1, '', 'command outcome is uncertain; inspect native transaction status before retrying')
        with patch.object(helper, 'command', return_value=uncertain) as run:
            with self.assertRaisesRegex(RuntimeError, 'uncertain'):
                pads.grid({'id': '7'})
        self.assertEqual(run.call_count, 1)

    def test_grid_retries_only_prewrite_busy_rejection(self):
        pads = helper.Scratchpads('kwabai')
        busy = subprocess.CompletedProcess([], 1, '', 'another geometry operation is unresolved; wait for confirmation')
        done = subprocess.CompletedProcess([], 0, '', '')
        with patch.object(helper, 'command', side_effect=[busy, done]) as run, patch.object(helper.time, 'sleep'):
            pads.grid({'id': '7'})
        self.assertEqual(run.call_count, 2)


if __name__ == '__main__':
    unittest.main()
