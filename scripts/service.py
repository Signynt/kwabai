#!/usr/bin/env python3
"""Install or control kwabai's per-user graphical-session service."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

MARKER = '# Managed by kwabai service helper v1\n'
UNIT = 'kwabai.service'


def quote(value):
    # systemd quoting is not shell quoting. Neutralize unit specifiers and
    # ExecStart environment expansion as well as whitespace and backslashes.
    if any(ord(char) < 32 for char in value):
        raise RuntimeError('Service paths cannot contain control characters')
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%').replace('$', '$$') + '"'


def unit_text(build, launcher):
    return MARKER + f'''[Unit]
Description=Kwabai KDE window management
PartOf=graphical-session.target
After=graphical-session-pre.target
StartLimitIntervalSec=60
StartLimitBurst=3

[Service]
Type=notify
NotifyAccess=main
ExecStart=/usr/bin/python3 {quote(str(launcher))} --build-dir {quote(str(build))} --enable-control --background
Restart=on-failure
RestartPreventExitStatus=2 78
RestartSec=15
TimeoutStartSec=120
TimeoutStopSec=10
KillMode=control-group
UMask=0077

[Install]
WantedBy=graphical-session.target
'''


def systemctl(*args):
    result = subprocess.run(['systemctl', '--user', *args], check=False)
    if result.returncode:
        raise RuntimeError(f'systemctl --user {args[0]} failed (status {result.returncode})')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['install', 'uninstall', 'start', 'stop', 'restart', 'status', 'print'])
    parser.add_argument('--build-dir', type=Path, default=Path(__file__).resolve().parent.parent / 'build')
    args = parser.parse_args()
    try:
        root = os.environ.get('XDG_CONFIG_HOME') or str(Path.home() / '.config')
        if not os.path.isabs(root):
            raise RuntimeError('XDG_CONFIG_HOME must be absolute')
        path = Path(root) / 'systemd/user' / UNIT
        build = args.build_dir.absolute()
        launcher = Path(__file__).resolve().with_name('start-session.py')
        if args.action == 'print':
            print(unit_text(build, launcher), end='')
            return 0
        if args.action == 'install':
            if not all((build / name).is_file() for name in ('kwabai', 'kwabaid')):
                raise RuntimeError('Build binaries are missing; build the project first')
            if path.is_symlink() or (path.exists() and not path.read_text().startswith(MARKER)):
                raise RuntimeError(f'Refusing to overwrite an unmanaged service: {path}')
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as stream:
                    temporary = stream.name
                    stream.write(unit_text(build, launcher))
                    stream.flush()
                    os.fsync(stream.fileno())
                os.chmod(temporary, 0o644)
                os.replace(temporary, path)
            finally:
                if temporary and os.path.exists(temporary):
                    os.unlink(temporary)
            systemctl('daemon-reload')
            print(f'Installed {path}; installation does not start or enable the service.')
            return 0
        if not path.is_file() or path.is_symlink() or not path.read_text().startswith(MARKER):
            raise RuntimeError('Managed service is not installed; use kwabai --install-service first')
        if args.action == 'uninstall':
            systemctl('disable', '--now', UNIT)
            path.unlink()
            systemctl('daemon-reload')
            print('Service removed; configuration, saved layouts and native effect installation are retained.')
        elif args.action == 'start':
            systemctl('reset-failed', UNIT)
            systemctl('enable', '--now', UNIT)
        elif args.action == 'stop':
            systemctl('stop', UNIT)
        elif args.action == 'restart':
            systemctl('reset-failed', UNIT)
            systemctl('restart', UNIT)
        else:
            return subprocess.run(['systemctl', '--user', 'status', '--no-pager', UNIT], check=False).returncode
        return 0
    except (OSError, RuntimeError) as error:
        print(f'kwabai service: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
