"""Configuration discovery and execution shared by session startup and reload."""
import os
from pathlib import Path
import signal
import subprocess
import time


class ConfigError(RuntimeError):
    pass


def discover(explicit=None):
    if explicit is not None:
        path = Path(explicit).expanduser().absolute()
        if not path.is_file():
            raise ConfigError(f'Configuration file does not exist: {path}')
        return path
    paths = []
    config_home = os.environ.get('XDG_CONFIG_HOME')
    if config_home:
        if not os.path.isabs(config_home):
            raise ConfigError('XDG_CONFIG_HOME must be absolute')
        paths.append(Path(config_home) / 'kwabai/kwabairc')
    paths += [Path.home() / '.config/kwabai/kwabairc', Path.home() / '.kwabairc']
    return next((path for path in paths if path.is_file()), None)


def terminate(process):
    if process is None:
        return
    # Config runs in its own process group so stopping a shell also stops its
    # foreground commands. Never signal the session or compositor process group.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


def run(path, build, socket, child, interrupted, timeout=30):
    env = dict(os.environ, PATH=str(build) + os.pathsep + os.environ.get('PATH', ''),
               KWABAI_SOCKET=str(socket), KWABAI_CONFIG_FILE=str(path))
    argv = [str(path)] if os.access(path, os.X_OK) else ['/bin/sh', str(path)]
    process = None
    try:
        try:
            process = subprocess.Popen(argv, env=env, stdin=subprocess.DEVNULL, start_new_session=True)
        except OSError as error:
            # Executable shell files without a shebang are accepted by sh, as
            # they are when invoked from yabai's shell execution path.
            if error.errno != 8:
                raise
            process = subprocess.Popen(['/bin/sh', str(path)], env=env, stdin=subprocess.DEVNULL, start_new_session=True)
        deadline = time.monotonic() + timeout
        while process.poll() is None:
            if interrupted[0]:
                raise ConfigError('Configuration interrupted')
            if child.poll() is not None:
                raise ConfigError('Daemon exited while configuration was running')
            if time.monotonic() >= deadline:
                raise ConfigError(f'Configuration timed out after {timeout:g} seconds: {path}')
            time.sleep(0.05)
        if process.returncode:
            raise ConfigError(f'Configuration exited with status {process.returncode}: {path}; earlier commands may already have applied')
    finally:
        if process is not None and process.poll() is None:
            terminate(process)
