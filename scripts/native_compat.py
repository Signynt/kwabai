"""Native bridge preflight. Never construct an effect in this process."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

REBUILD_EXIT = 78


class RebuildRequired(RuntimeError):
    pass


def run(argv, timeout=5):
    try:
        return subprocess.run(argv, text=True, capture_output=True, timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RebuildRequired(f'Cannot check native bridge: {error}') from error


def installed_version():
    result = run(['kwin_wayland', '--version'])
    match = re.search(r'\bkwin\s+(\d+\.\d+\.\d+)\b', result.stdout)
    if result.returncode or not match:
        raise RebuildRequired('Cannot determine installed KWin version; verify the KWin installation')
    return match[1]


def manifest(build):
    path = build / 'kwabai-native-build.json'
    if not path.is_file():
        path = Path(__file__).resolve().parent / 'kwabai-native-build.json'
    try:
        data = json.loads(path.read_text())
        if (data.get('schema') != 1 or not re.fullmatch(r'\d+\.\d+\.\d+', data.get('kwin-version', ''))
                or not re.fullmatch(r'kwabai_probe_[a-z0-9_]+', data.get('effect-id', ''))):
            raise ValueError('invalid manifest')
        effect_file = build / 'kwabai-effect-id'
        if not effect_file.is_file():
            effect_file = Path(__file__).resolve().parent / 'kwabai-effect-id'
        if effect_file.read_text().strip() != data['effect-id']:
            raise ValueError('effect ID and native manifest disagree')
        return data
    except (OSError, ValueError, TypeError, AttributeError) as error:
        raise RebuildRequired('Native build metadata is missing or stale; rebuild required') from error


def check(build, running=None, artifact=None):
    build = Path(build).resolve()
    data = manifest(build)
    installed = installed_version()
    if data['kwin-version'] != installed:
        raise RebuildRequired(f"Rebuild required: bridge targets KWin {data['kwin-version']}, installed KWin is {installed}")
    if running is not None and running != installed:
        raise RebuildRequired(f'KWin session restart required: running {running}, installed {installed}; log out and back in before loading the rebuilt bridge')
    checker = build / 'kwabai-plugin-check'
    if not checker.is_file() or not os.access(checker, os.X_OK):
        raise RebuildRequired('Native compatibility checker is missing; rebuild required')
    if artifact is None:
        artifact = build / 'plugins/kwin/effects/plugins' / (data['effect-id'] + '.so')
        if not artifact.is_file():
            paths = run(['qtpaths6', '--query', 'QT_INSTALL_PLUGINS'])
            if paths.returncode == 0:
                artifact = Path(paths.stdout.strip()) / 'kwin/effects/plugins' / (data['effect-id'] + '.so')
    if not Path(artifact).is_file():
        raise RebuildRequired('Native effect is missing; rebuild required')
    result = run([str(checker), str(artifact), data['effect-id']], timeout=15)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or f'checker status {result.returncode}'
        raise RebuildRequired('Native bridge is incompatible; rebuild required: ' + detail)
    try:
        compiled = json.loads(result.stdout)
        if compiled.get('kwin-version') != installed or compiled.get('effect-id') != data['effect-id']:
            raise ValueError('checker and manifest differ')
    except (ValueError, AttributeError) as error:
        raise RebuildRequired('Embedded native build identity is stale; rebuild required') from error
    return data


def running_version(support):
    match = re.search(r'^KWin version:\s*(\d+\.\d+\.\d+)\b', support, re.MULTILINE)
    if not match:
        raise RebuildRequired('Cannot determine running KWin version; refusing to load an unchecked native bridge')
    return match[1]


if __name__ == '__main__':
    try:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('build', nargs='?', type=Path)
        parser.add_argument('--installed-version', action='store_true')
        parser.add_argument('--running-session', action='store_true')
        args = parser.parse_args()
        if args.installed_version:
            print(installed_version())
            sys.exit(0)
        if args.build is None:
            parser.error('build directory required')
        running = None
        if args.running_session:
            support = run(['qdbus6', 'org.kde.KWin', '/KWin', 'org.kde.KWin.supportInformation'])
            if support.returncode:
                raise RebuildRequired('KWin session is unavailable; do not reload until the session is ready')
            running = running_version(support.stdout)
        data = check(args.build, running=running)
        print(f"Native bridge verified for KWin {data['kwin-version']}: {data['effect-id']}")
    except (RebuildRequired, IndexError) as error:
        print(str(error) + '\nRun ./scripts/reload-effect.sh from the project folder.', file=sys.stderr)
        sys.exit(REBUILD_EXIT)
