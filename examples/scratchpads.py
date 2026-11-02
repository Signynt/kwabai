#!/usr/bin/env python3
"""Restart scratchpads, or toggle one and launch it when closed. KDE owns shortcuts."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass


@dataclass(frozen=True)
class App:
    name: str
    pattern: str
    desktop: str
    window_close_only: bool = False


APPS = (
    App('Spotify', r'^[Ss]potify$', 'spotify-launcher'),
    App('Calendar', r'^ThunderCalendar$', 'thunderbird-calendar'),
    App('Reminders', r'^chrome-www\.icloud\.com__reminders_-Default$', 'reminders', True),
    App('Mail', r'^(org\.mozilla\.Thunderbird|thunderbird)$', 'org.mozilla.Thunderbird'),
    App('whatsapp', r'^elecwhat$', 'elecwhat'),
)
GRID = '11:11:1:1:9:9'


def command(argv):
    return subprocess.run([str(arg) for arg in argv], capture_output=True, text=True, timeout=8)


def checked(argv):
    result = command(argv)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or f'Command failed: {argv[0]}')
    return result.stdout


def find_kwabai():
    if os.environ.get('KWABAI_BIN'):
        return os.environ['KWABAI_BIN']
    for candidate in (Path(__file__).resolve().parents[1] / 'build/kwabai',):
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return shutil.which('kwabai') or 'kwabai'


class Scratchpads:
    def __init__(self, binary, timeout=30):
        self.binary = binary
        self.timeout = timeout
        self.launchers = []

    def cli(self, *args):
        return checked([self.binary, *args])

    def windows(self):
        # Native UUIDs are needed by kdotool; numeric query IDs are different.
        return json.loads(self.cli('bridge', 'windows'))['windows']

    def ready(self):
        doctor = json.loads(self.cli('doctor'))
        if not doctor.get('bridge-connected') or not doctor.get('desktop-control'):
            raise RuntimeError('Start the control-enabled kwabai service first.')
        caps = doctor.get('bridge', {}).get('capabilities', [])
        if not {'scratchpads', 'floating-grid'}.issubset(caps):
            raise RuntimeError('Load the current kwabai effect first: ./scripts/reload-effect.sh')

    def rule(self, app):
        # Named persistent rules are idempotent; kwabai has no --one-shot yet.
        self.cli('-m', 'rule', '--add', f'label=scratchpad-{app.name}',
                 f'app={app.pattern}', f'scratchpad={app.name}', f'grid={GRID}')

    @staticmethod
    def matches(app, window):
        return re.search(app.pattern, window.get('app', '')) is not None

    def assigned(self, app, windows):
        matches = [w for w in windows if w.get('scratchpad') == app.name]
        if len(matches) > 1 or (matches and not self.matches(app, matches[0])):
            raise RuntimeError(f'Scratchpad {app.name} is assigned to another app; inspect kwabai query --windows.')
        return matches[0] if matches else None

    def launch(self, app):
        # Desktop entries preserve Thunderbird profiles and Chromium's app URL.
        process = subprocess.Popen(['gtk-launch', app.desktop], stdin=subprocess.DEVNULL,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   start_new_session=True)
        self.launchers.append((app, process))

    def wait_assigned(self, app):
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            window = self.assigned(app, self.windows())
            if window:
                return window
            for launched, process in self.launchers:
                if launched == app and process.poll() not in (None, 0):
                    raise RuntimeError(f'Could not launch {app.name} using {app.desktop}.desktop')
            time.sleep(0.15)
        raise RuntimeError(f'Timed out waiting for {app.name}; check kwabai doctor scratchpad-policy errors.')

    def grid(self, window):
        # Assignment can become visible before the previous tile releases ownership.
        # Retry only errors known to occur before a new geometry write is sent.
        deadline = time.monotonic() + self.timeout
        while True:
            result = command([self.binary, '-m', 'window', window['id'], '--grid', GRID])
            if not result.returncode:
                return
            error = result.stderr.strip()
            pending = any(text in error for text in (
                'another geometry operation is unresolved', 'mouse interaction in progress',
                'output drop in progress', 'context transfer in progress',
                'grid requires a floating window; toggle float or assign a scratchpad first'))
            if not pending or time.monotonic() >= deadline:
                raise RuntimeError(error or 'Grid placement failed; inspect kwabai doctor.')
            time.sleep(0.15)

    def toggle(self, app):
        self.ready()
        self.rule(app)
        windows = self.windows()
        if self.assigned(app, windows):
            self.cli('-m', 'window', '--toggle', app.name)
            return
        existing = any(self.matches(app, w) for w in windows)
        if not existing:
            self.launch(app)
        window = self.wait_assigned(app)
        # Hidden windows need to be summoned before accepting geometry.
        if window.get('scratchpad-hidden'):
            self.cli('-m', 'window', '--toggle', app.name)
        else:
            self.grid(window)
            if existing:
                self.cli('-m', 'window', '--toggle', app.name)
        # A freshly opened app stays visible, matching "toggle || open".

    @staticmethod
    def alive(pid):
        try:
            os.kill(pid, 0)
            return True
        except ProcessLookupError:
            return False

    def restart(self):
        self.ready()
        for app in APPS:
            self.rule(app)
        original = self.windows()
        old_ids = set()
        terminate = set()
        close = []
        for window in original:
            app = next((app for app in APPS if self.matches(app, window)), None)
            if app is None:
                continue
            uuid = window['uuid']
            old_ids.add(uuid)
            kwin_id = '{' + uuid.strip('{}') + '}'
            if app.window_close_only:
                close.append(kwin_id)
                continue
            pid = checked(['kdotool', 'getwindowpid', kwin_id]).strip()
            if pid.isdecimal() and int(pid) > 1 and int(pid) not in (os.getpid(), os.getppid()):
                terminate.add(int(pid))
            else:
                close.append(kwin_id)
        for kwin_id in close:
            checked(['kdotool', 'windowclose', kwin_id])
        for pid in terminate:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        deadline = time.monotonic() + self.timeout
        while old_ids.intersection(w['uuid'] for w in self.windows()) or any(self.alive(pid) for pid in terminate):
            if time.monotonic() >= deadline:
                raise RuntimeError('Some scratchpad apps did not exit. Finish any app dialogs, then retry.')
            time.sleep(0.15)
        # All apps start before waiting for any one of them, keeping startup parallel.
        for app in APPS:
            self.launch(app)
        for app in APPS:
            window = self.wait_assigned(app)
            if not window.get('scratchpad-hidden'):
                self.grid(window)
                self.cli('-m', 'window', '--toggle', app.name)
            print(f'{app.name}: ready (hidden)', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('restart', 'toggle'), nargs='?', default='restart')
    parser.add_argument('name', nargs='?', choices=[app.name for app in APPS])
    parser.add_argument('--timeout', type=float, default=30, help='seconds to wait for each app (default: 30)')
    parser.add_argument('--dry-run', action='store_true', help='show mappings; make no window/app/rule changes')
    args = parser.parse_args()
    if (args.action == 'toggle') != bool(args.name):
        parser.error('use restart, or toggle NAME')
    if not 0 < args.timeout <= 300:
        parser.error('--timeout must be greater than 0 and at most 300')
    selected = [app for app in APPS if not args.name or app.name == args.name]
    if args.dry_run:
        for app in selected:
            print(f'{app.name}: app={app.pattern} launcher={app.desktop}.desktop grid={GRID}')
        return 0
    try:
        for dependency in ('gtk-launch', 'kdotool') if args.action == 'restart' else ('gtk-launch',):
            if not shutil.which(dependency):
                raise RuntimeError(f'Required command is missing: {dependency}')
        # Serialize rapid shortcut presses and restarts to avoid duplicate launches.
        runtime = Path(os.environ.get('XDG_RUNTIME_DIR', f'/run/user/{os.getuid()}'))
        fd = os.open(runtime / 'kwabai-scratchpads.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        with os.fdopen(fd, 'w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            pads = Scratchpads(find_kwabai(), args.timeout)
            if args.action == 'restart':
                pads.restart()
            else:
                pads.toggle(selected[0])
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f'scratchpads: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
