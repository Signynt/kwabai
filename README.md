# kwabai

**A dynamic tiling window manager for KDE Plasma, inspired by [yabai](https://github.com/asmvik/yabai).**

kwabai adds binary space partitioning (BSP) layouts to KWin on Wayland. It tiles
windows automatically across monitors and virtual desktops, supports mouse-driven
layout editing, and provides a command-line interface for scripts and keyboard
shortcuts. A native KWin effect handles desktop integration; a separate daemon
maintains layouts and processes commands.

The goal is to reproduce yabai's behavior and command interface on KDE. A useful
subset is implemented, but **kwabai is not yet a complete drop-in replacement**.
Linux application IDs and KDE desktop semantics also differ from macOS.

https://github.com/user-attachments/assets/b72a8381-32d1-40ba-93b5-ef676db3b5fa

**Status: early development.** The native effect runs inside KWin, so a defect
can affect the desktop session. Development and live testing have used KDE
Plasma Wayland on Arch-based Linux. Other distributions and KWin releases need
validation; there is no stable ABI or prebuilt cross-version plugin.

## Features

- Automatic BSP tiling, with separate layouts for outputs, virtual desktops and KDE Activities.
- Reflow when windows open, close, minimize, return or move between contexts.
- Mouse resizing, center drops to swap, and edge drops to split tiles, including across monitors.
- Drop previews with configurable color and rounded corners.
- Directional focus, swaps, split ratios, resizing, rotation, mirroring and balancing.
- Floating windows, fullscreen zoom, window layers and named scratchpads.
- Application/title rules, including exclusions, layers, scratchpad assignment and floating grids.
- Persistent layout state and rules, per-context undo/redo, and a systemd user service.

kwabai does **not** register keyboard shortcuts. Bind the commands you want in
KDE System Settings → Keyboard → Shortcuts. Disable other automatic tiling
scripts or extensions before enabling kwabai, so they do not compete for windows.

## Requirements

- KDE Plasma with a running KWin Wayland session and systemd user services.
- KWin development headers matching the installed compositor.
- A C++20 compiler, CMake 3.25 or newer, Extra CMake Modules, Qt 6, KDE CoreAddons
  and Plasma Activities development files.
- Python 3 and `qdbus6` for session/service helpers.
- Node.js and a D-Bus session runner (`dbus-run-session`) for the full test suite.

The drop preview requires KWin's OpenGL backend. Native effects must be rebuilt
when KWin changes; the reload helper checks the build against both installed and
running KWin.

## Build and start

The following setup uses an Arch-based distribution and runs kwabai from its
checkout. Other distributions need the equivalent development packages. The
reload helper currently installs into `/usr/lib/qt6/plugins/kwin/effects/plugins`;
other plugin locations require adapting that path.

From the project folder:

```sh
sudo pacman -S --needed base-devel cmake extra-cmake-modules kwin \
    qt6-base qt6-tools kcoreaddons plasma-activities python nodejs dbus

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
```

KWin's CMake package may request additional development dependencies on your
system; install those before continuing.

### 1. Install and load the native effect

Run this in a terminal within your KDE session:

```sh
./scripts/reload-effect.sh
```

The helper builds and checks the plugin, asks for administrator privileges to
install it, and loads it into KWin. It uses a versioned filename to avoid stale
plugin caching. Building alone does not start a service or enable tiling.

### 2. Create a configuration

Create `~/.config/kwabai/kwabairc` with your editor:

```sh
#!/bin/sh
set -eu

kwabai -m config layout bsp
kwabai -m config top_padding 0
kwabai -m config bottom_padding 10
kwabai -m config left_padding 10
kwabai -m config right_padding 10
kwabai -m config window_gap 10
kwabai -m config insert_feedback_color 0xff33a4f5
kwabai -m config window_border_radius 8

# Use Linux application IDs reported by the window query command.
kwabai -m rule --add 'app=^(systemsettings|org\.kde\.systemsettings)$' manage=off
```

`layout bsp` enables automatic management across displays and desktops; no
per-display `manage` command is needed. The default layout is `float`.

`insert_feedback_color` uses `0xAARRGGBB`. `window_border_radius` sets the
drop-preview corner radius from 0 (square) to 20 pixels.

The launcher selects the first existing configuration in this order:

1. `$XDG_CONFIG_HOME/kwabai/kwabairc`, when `XDG_CONFIG_HOME` is set.
2. `~/.config/kwabai/kwabairc`.
3. `~/.kwabairc`.

A non-executable file runs with `/bin/sh`; an executable file uses its shebang.
Configuration runs after bridge readiness, with the matching `kwabai` binary on
`PATH`. A failing configuration stops startup. `set -eu` makes the shell stop
on a failed command or an unset variable.

### 3. Start the user service

```sh
./build/kwabai --install-service
./build/kwabai --start-service
./build/kwabai doctor
```

Installation creates an inactive user unit. Starting it also enables it for
future graphical logins. Expect `bridge-connected` and `desktop-control` to be
`true` in `doctor` output.

For a foreground session instead, run
`./scripts/start-session.sh --enable-control` and keep that terminal open. Do not
run a foreground daemon and the service at the same time.

## Commands and shortcuts

Use `./build/kwabai` from the checkout, or its absolute path in KDE shortcuts.
If you add the checkout's `build` directory to your shell's `PATH`, the examples
below can be used as written. No global command is installed by the build steps.

Commands without a window selector normally use the focused window. Numeric
window IDs can be obtained from queries and remain valid for the daemon session.

```sh
kwabai -m query --windows id,app,title,is-floating
kwabai -m window --focus east
kwabai -m window --swap south
kwabai -m window --ratio rel:0.05
kwabai -m window --resize right:50:0
kwabai -m window --toggle split
kwabai -m window --toggle float
kwabai -m window --toggle zoom-fullscreen
kwabai -m space --rotate 90
kwabai -m space --mirror x-axis
kwabai -m space --equalize
```

Fullscreen zoom fills the context's padded work area while retaining layout
membership; it is separate from an application's native fullscreen mode.
Floating a window leaves it at its current position and size.

Some additional supported commands:

| Task | Command |
| --- | --- |
| Focus a tiled window by ID | `kwabai -m window --focus 12` |
| Move a window to the display on the left | `kwabai -m window --display west` |
| Send a window to virtual desktop 2 | `kwabai -m window --space 2` |
| Change the current layout's gap | `kwabai -m space --gap abs:20` |
| Set current layout padding, top/bottom/left/right | `kwabai -m space --padding abs:0:10:10:10` |
| Set a window's explicit layer | `kwabai -m window --layer above` |
| Return to automatic layering | `kwabai -m window --layer auto` |
| Undo / redo a layout edit | `kwabai context undo focused` / `kwabai context redo focused` |

Window transfers do not switch your desktop or focus another display. Use KDE's
own actions for desktop switching, or combine them with commands in your shortcuts.

### Rules

Rules use POSIX extended regular expressions for application IDs and titles.
Query IDs with `kwabai -m query --windows id,app,title`; Linux IDs are not macOS
application names. Later matching rules override earlier matching actions.

```sh
kwabai -m rule --add 'app=^org\.kde\.kruler$' manage=off
kwabai -m rule --add 'app=^(org\.kde\.dolphin|dolphin)$' \
    'title=^Properties for ' manage=off
kwabai -m rule --add label=settings 'app=^systemsettings$' manage=off layer=above
kwabai -m rule --list
kwabai -m rule --remove settings
```

Labels are optional. Reusing a label replaces that rule; unnamed rules can be
removed by their listed index. Rules persist across service restarts. Rules are
primarily admission policy, not a general command to reapply every property to
all existing windows.

Non-normal window types are excluded from tiling. The default-on
`kwabai -m config float_popups on` also floats normal windows marked modal or
transient by KWin. Explicit `manage=on` rules can override that default for
otherwise eligible windows. Apps that provide no popup hints need a matching
exclusion rule.

### Scratchpads

Assign the focused window to a name, then show or hide it with that name:

```sh
kwabai -m window --scratchpad terminal
kwabai -m window --toggle terminal
```

A shown scratchpad floats on the current output, desktop and Activity. Automatic
stacking puts scratchpads above fullscreen zoom, ordinary floating windows and
tiles. Explicit layer overrides and forced KDE rules can change this order.
Wayland scratchpads use KWin's hidden state; X11/Xwayland scratchpads use
minimization to preserve taskbar participation.

Assign scratchpads automatically with a rule:

```sh
kwabai -m rule --add 'app=^org\.kde\.konsole$' scratchpad=terminal grid=11:11:1:1:9:9
```

Grid values are `rows:columns:x:y:width:height`. Use a rule that identifies the
particular window you want; a scratchpad name represents one window.

Remove the focused window's assignment with `kwabai -m window --scratchpad`, or
reveal hidden scratchpads with `kwabai -m window --scratchpad recover`. Assignments
survive daemon restarts while the native effect and windows remain alive; they
do not relaunch applications after logout or reboot.

### Compatibility boundaries

`-m` and `--message` support a subset of yabai's `config`, `window`, `space`,
`query` and `rule` interfaces. Unsupported commands fail explicitly. Full query
schemas, selectors, signals, layouts and command coverage are unfinished. Do not
assume an existing yabai configuration can run unchanged. macOS-specific scripting
additions, SIP settings and Dock injection do not apply to KDE; `window_shadow`
is not implemented and shadows remain controlled by KDE.

To run supported commands in scripts that literally call `yabai`, opt in from
the project folder with `export PATH="$PWD/scripts/compat:$PATH"`. This optional
wrapper forwards to this checkout's `build/kwabai` (or `KWABAI_BIN` if set); it
does not add missing yabai features or install a global command.

## Service, updates and recovery

```sh
./build/kwabai --service-status
./build/kwabai --restart-service    # Also reruns the configuration
./build/kwabai --stop-service
journalctl --user -u kwabai.service -b --no-pager -n 80
```

Stop leaves windows where they are; it does not restore an old arrangement.
Stopping leaves login startup enabled. To disable it permanently:

```sh
systemctl --user disable --now kwabai.service
```

After a KWin update, log out and back in if the running compositor is still the
old version, then run:

```sh
./build/kwabai --stop-service
./scripts/reload-effect.sh
./build/kwabai --start-service
```

The reload helper uses a fresh build cache when KWin changes and checks embedded
metadata and library imports before installation or unloading. Startup waits for
KWin during login. An incompatible bridge reports **rebuild required** and stops
automatic restart attempts. After updating kwabai's service settings, rerun
`./build/kwabai --install-service` to refresh an existing unit.

Use `./build/kwabai doctor` for bridge and management diagnostics. To rebuild stale
visible-context membership, use `./build/kwabai context reset all`, then re-enable
BSP with `./build/kwabai -m config layout bsp`. Reset discards those contexts'
current layout trees; it is not an undo operation.

State is stored under `$XDG_STATE_HOME/kwabai` or `~/.local/state/kwabai`. The
control socket lives at `$XDG_RUNTIME_DIR/kwabai/control.sock`; it does not require
a manually created `/tmp` directory after reboot. Uninstalling the user service
with `./build/kwabai --uninstall-service` preserves configuration, state and the
native effect installation.

## Development and limitations

```sh
ctest --test-dir build --output-on-failure -j2
```

Tests include the BSP core, CLI and daemon integration, isolated fake-KWin
sessions, and native plugin discovery without constructing an effect. These do
not replace live testing across KWin versions, applications and display setups.

Current limits include 20 tiled windows per context and 64 stored contexts.
Application minimum/maximum sizes can constrain layouts, and behavior still
needs testing with more fractional scaling, hotplug and application combinations.
Fullscreen zoom and floating are supported; desktop integration of window stacks
is unfinished. Undo/redo history is kept in memory and clears on restart or
membership changes. X11 desktop sessions are not a validated target.

When reporting a problem, include your distribution, KWin version, relevant
configuration, reproduction steps, and `doctor`/service log output. Window queries
and logs can contain window titles, application IDs and local paths; review them
before sharing.

## License and attribution

The original kwabai source is available under the [MIT License](LICENSE).
[yabai](https://github.com/asmvik/yabai) is the reference for its behavior and
command interface; kwabai is an independent project. The optional `yabai` wrapper
forwards commands to kwabai and does not bundle the upstream yabai program.

See [third-party notices](THIRD_PARTY_NOTICES.md) for yabai's attribution and
license, and dependency licensing. In particular, distributing compiled native
KWin-linked plugins requires observing the applicable GPL terms in addition to
preserving kwabai's MIT notice.
