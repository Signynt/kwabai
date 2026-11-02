#!/usr/bin/env python3
"""Start one foreground kwabai observer using the installed native bridge."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import stat
import socket as unix_socket
import subprocess
import sys
import time
from native_compat import REBUILD_EXIT, RebuildRequired, check as check_native, running_version
from session_config import ConfigError, discover, run as run_config


def private_directory(path):
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid() or info.st_mode & 0o077:
        raise RuntimeError(f"Directory must be owned by you, not a symlink, and mode 0700: {path}")


def command(args, timeout=4):
    try:
        return subprocess.run(args, text=True, capture_output=True, timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RuntimeError(f"Could not run {args[0]}: {error}") from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--enable-control', action='store_true', help='allow explicit geometry commands; management still starts disabled')
    config = parser.add_mutually_exclusive_group()
    config.add_argument('-c', '--config', type=Path, help='shell configuration to run after bridge readiness')
    config.add_argument('--no-config', action='store_true', help='skip automatic shell configuration')
    parser.add_argument('--config-timeout', type=float, default=30, help='configuration deadline, 1 to 300 seconds')
    parser.add_argument('--background', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--build-dir', type=Path, default=Path(__file__).resolve().parent.parent / 'build')
    parser.add_argument('--socket', type=Path, help='default: $XDG_RUNTIME_DIR/kwabai/control.sock')
    parser.add_argument('--state-dir', type=Path, help='default: $XDG_STATE_HOME/kwabai or ~/.local/state/kwabai')
    parser.add_argument('--kwin-timeout', type=float, default=45, help='wait for KWin during login, 1 to 60 seconds')
    parser.add_argument('--timeout', type=float, default=10, help='readiness deadline in seconds (1 to 60)')
    args = parser.parse_args()
    if not 1 <= args.config_timeout <= 300:
        parser.error('--config-timeout must be between 1 and 300 seconds')
    if not 1 <= args.kwin_timeout <= 60:
        parser.error('--kwin-timeout must be between 1 and 60 seconds')
    if not 1 <= args.timeout <= 60:
        parser.error('--timeout must be between 1 and 60 seconds')
    runtime = os.environ.get('XDG_RUNTIME_DIR', '')
    if args.socket is None and not os.path.isabs(runtime):
        parser.error('an absolute XDG_RUNTIME_DIR or explicit --socket is required')
    state_home = os.environ.get('XDG_STATE_HOME') or str(Path.home() / '.local/state')
    if args.state_dir is None and not os.path.isabs(state_home):
        parser.error('XDG_STATE_HOME must be absolute, or provide --state-dir')
    socket = Path(os.path.abspath(args.socket or Path(runtime) / 'kwabai/control.sock'))
    state_dir = Path(os.path.abspath(args.state_dir or Path(state_home) / 'kwabai'))
    build = args.build_dir.resolve()
    child = None
    interrupted = [0]
    old_handlers = {}
    try:
        configuration = None if args.no_config else discover(args.config)
        cli, daemon = build / 'kwabai', build / 'kwabaid'
        if not all(path.is_file() and os.access(path, os.X_OK) for path in (cli, daemon)):
            raise RebuildRequired('Build binaries are missing; run cmake --build build -j2 first')
        effect_file = build / 'kwabai-effect-id'
        if not effect_file.is_file():
            effect_file = Path(__file__).resolve().parent / 'kwabai-effect-id'
        try:
            effect_id = effect_file.read_text().strip()
        except OSError as error:
            raise RebuildRequired('Native build metadata is missing; rebuild required') from error
        if not re.fullmatch(r'kwabai_probe_[a-z0-9_]+', effect_id):
            raise RebuildRequired('Invalid native effect build ID; rebuild required')
        private_directory(socket.parent)
        private_directory(state_dir)
        existing = command([str(cli), '--socket', str(socket), 'doctor'])
        if existing.returncode == 0:
            raise RuntimeError(f'A daemon already answers on {socket}; stop it before starting another session')
        deadline = time.monotonic() + args.kwin_timeout
        print('Waiting for KWin session...', flush=True)
        while True:
            try:
                effects = command(['qdbus6', 'org.kde.KWin', '/Effects', 'org.kde.kwin.Effects.loadedEffects'], timeout=min(4, max(0.1, deadline-time.monotonic())))
                last_kwin_error = effects.stderr.strip()
            except RuntimeError as error:
                effects = None
                last_kwin_error = str(error)
            if effects is not None and effects.returncode == 0:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError('KWin session was not ready before the login deadline: ' + last_kwin_error)
            time.sleep(0.2)
        support = command(['qdbus6', 'org.kde.KWin', '/KWin', 'org.kde.KWin.supportInformation'])
        if support.returncode:
            raise RebuildRequired('Cannot inspect running KWin; refusing to load an unchecked native bridge')
        check_native(build, running=running_version(support.stdout))
        loaded = effects.stdout.split()
        if any(name != effect_id and name.startswith(('kwabai_probe', 'libkwabai_probe')) for name in loaded):
            raise RebuildRequired('An older kwabai effect is loaded; stop the daemon and run ./scripts/reload-effect.sh')
        if effect_id not in loaded:
            result = command(['qdbus6', 'org.kde.KWin', '/Effects', 'org.kde.kwin.Effects.loadEffect', effect_id])
            if result.returncode or result.stdout.strip() != 'true':
                raise RebuildRequired('Rebuild required: could not load the installed build; run ./scripts/reload-effect.sh in your terminal, then retry')
        snapshot = command(['qdbus6', 'org.kde.KWin', '/org/kwabai/Bridge', 'org.kwabai.Bridge1.Snapshot'])
        try:
            bridge = json.loads(snapshot.stdout)
            valid = (snapshot.returncode == 0 and isinstance(bridge, dict)
                     and {'snapshot', 'contexts', 'events', 'geometry', 'geometry-update', 'geometry-no-restore'}.issubset(bridge.get('capabilities', []))
                     and bridge.get('geometry-window-limit', 2) == 20
                     and bridge.get('kwin-version') == running_version(support.stdout))
        except (ValueError, TypeError):
            valid = False
        if not valid:
            raise RebuildRequired('Loaded bridge is incompatible; rebuild required; run ./scripts/reload-effect.sh')
        argv = [str(daemon), '--socket', str(socket), '--observe-kwin', '--context-state', str(state_dir / 'contexts.json')]
        if args.enable_control:
            argv.append('--enable-control')

        def forward(signum, _frame):
            interrupted[0] = signum
            if child is not None and child.poll() is None:
                try:
                    child.send_signal(signum)
                except ProcessLookupError:
                    pass

        for signum in (signal.SIGINT, signal.SIGTERM):
            old_handlers[signum] = signal.signal(signum, forward)
        child = subprocess.Popen(argv, stdin=subprocess.DEVNULL)
        deadline, last_error = time.monotonic() + args.timeout, 'waiting for bridge'
        while time.monotonic() < deadline and not interrupted[0]:
            if child.poll() is not None:
                raise RuntimeError(f'Daemon exited before readiness (status {child.returncode})')
            try:
                reply = command([str(cli), '--socket', str(socket), 'doctor'], timeout=min(3, max(0.1, deadline-time.monotonic())))
                doctor = json.loads(reply.stdout) if reply.returncode == 0 else {}
                ready = (doctor.get('bridge-connected') is True and doctor.get('event-stream', {}).get('ready') is True
                         and doctor.get('desktop-control') is args.enable_control
                         and doctor.get('pid') == child.pid)
                if ready:
                    if configuration is not None:
                        print(f'Loading configuration: {configuration}', flush=True)
                        begin = command([str(cli), '--socket', str(socket), 'context', 'configure', 'begin'])
                        if begin.returncode:
                            raise ConfigError(begin.stderr.strip() or 'Could not begin configuration')
                        run_config(configuration, build, socket, child, interrupted, args.config_timeout)
                        end = command([str(cli), '--socket', str(socket), 'context', 'configure', 'end'])
                        if end.returncode:
                            raise ConfigError(end.stderr.strip() or 'Could not finish configuration')
                    notify = os.environ.get('NOTIFY_SOCKET')
                    if notify:
                        address = '\0' + notify[1:] if notify.startswith('@') else notify
                        with unix_socket.socket(unix_socket.AF_UNIX, unix_socket.SOCK_DGRAM) as notifier:
                            notifier.connect(address)
                            notifier.sendall(b'READY=1\nSTATUS=Bridge and configuration ready')
                    print(f"Session ready ({'control enabled' if args.enable_control else 'observation only'}); {'configuration applied' if configuration else 'management is disabled'}.", flush=True)
                    print(f'Socket: {socket}\nState: {state_dir / "contexts.json"}', flush=True)
                    if not args.background:
                        print('Keep this terminal open; Ctrl+C stops this daemon.', flush=True)
                    return child.wait()
                last_error = doctor.get('bridge', {}).get('error') or reply.stderr.strip() or last_error
            except ConfigError:
                raise
            except (ValueError, RuntimeError) as error:
                last_error = str(error)
            time.sleep(0.1)
        if interrupted[0]:
            return 128 + interrupted[0]
        raise RuntimeError(f'Bridge readiness timed out: {last_error}')
    except RebuildRequired as error:
        message = f'kwabai session: {error}. Run ./scripts/reload-effect.sh from the project folder.'
        print(message, file=sys.stderr)
        notify = os.environ.get('NOTIFY_SOCKET')
        if notify:
            try:
                address = '\0' + notify[1:] if notify.startswith('@') else notify
                with unix_socket.socket(unix_socket.AF_UNIX, unix_socket.SOCK_DGRAM) as notifier:
                    notifier.connect(address)
                    notifier.sendall(('STATUS=' + message.replace('\n', ' ') + '\n').encode())
            except OSError:
                pass
        return REBUILD_EXIT
    except ConfigError as error:
        print(f'kwabai config: {error}', file=sys.stderr)
        return 2
    except (OSError, RuntimeError) as error:
        print(f'kwabai session: {error}', file=sys.stderr)
        return 1
    finally:
        if child is not None and child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=4)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
        for signum, handler in old_handlers.items():
            signal.signal(signum, handler)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
