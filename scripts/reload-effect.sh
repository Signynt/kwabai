#!/bin/sh
# Run from a user terminal: sudo prompts there, never in the compositor.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
if [ "${1-}" != "--print" ]; then
    if [ "$#" -ne 0 ]; then echo 'Usage: scripts/reload-effect.sh [--print]' >&2; exit 2; fi
    installed=$(python3 scripts/native_compat.py --installed-version)
    previous=$(python3 - <<'PY_VERSION'
import json
from pathlib import Path
try:
    print(json.loads(Path('build/kwabai-native-build.json').read_text()).get('kwin-version', ''))
except (OSError, ValueError, AttributeError):
    print('')
PY_VERSION
    )
    if [ "$previous" != "$installed" ]; then
        echo "KWin changed (bridge: ${previous:-unknown}, installed: $installed); rebuilding with a fresh CMake cache."
        cmake --fresh -S . -B build -DCMAKE_BUILD_TYPE=Debug
        cmake --build build --clean-first -j2
    else
        cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
        cmake --build build -j2
    fi
    # Check embedded metadata and eagerly resolve imports in a separate process.
    # A stale moc object or incremental build is retried once with a clean build.
    if ! python3 scripts/native_compat.py build; then
        echo 'Native preflight failed; forcing one clean rebuild.'
        cmake --build build --clean-first -j2
        python3 scripts/native_compat.py build
    fi
    # Package updates can leave an older compositor running until logout.
    # Refuse before sudo, unloading, or trying to load into that older process.
    python3 scripts/native_compat.py build --running-session
fi
id=$(cat build/kwabai-effect-id)
case "$id" in kwabai_probe_*) ;; *) echo 'Invalid effect build ID' >&2; exit 1;; esac
case "$id" in *[!a-z0-9_]*) echo 'Invalid effect build ID' >&2; exit 1;; esac
artifact="$project_dir/build/plugins/kwin/effects/plugins/$id.so"
if [ ! -f "$artifact" ]; then echo 'Build artifact missing' >&2; exit 1; fi
if [ "${1-}" = "--print" ]; then printf '%s\n%s\n' "$id" "$artifact"; exit 0; fi
# Install before unloading: failed authentication leaves the current effect intact.
sudo install -Dm755 "$artifact" "/usr/lib/qt6/plugins/kwin/effects/plugins/$id.so"
loaded=$(qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadedEffects)
for effect in $loaded; do
    case "$effect" in
        kwabai_probe|kwabai_probe_*|libkwabai_probe)
            qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect "$effect"
            ;;
    esac
done
result=$(qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect "$id")
if [ "$result" != true ]; then echo "KWin rejected $id" >&2; exit 1; fi
qdbus6 org.kde.KWin /org/kwabai/Bridge org.kwabai.Bridge1.Snapshot | python3 -c 'import json,sys; s=json.load(sys.stdin); assert {"contexts","events","geometry","geometry-no-restore","geometry-update","geometry-retention","geometry-lifecycle","notifications","focus","geometry-release","output-move","desktop-move","mouse"}.issubset(s.get("capabilities",[])), "KWin still serves an older bridge"; assert s.get("geometry-window-limit",2)==20, "KWin still serves the older geometry limit"; assert "scratchpads" not in s.get("capabilities", []) or all(isinstance(w.get("scratchpad"), str) and isinstance(w.get("scratchpad-hidden"), bool) for w in s.get("windows", [])), "Native bridge omitted scratchpad window metadata"; print("Loaded bridge with contexts, events and geometry on",len(s["outputs"]),"outputs; window limit:",s["geometry-window-limit"]); print("Mouse drop previews:", "available" if "drop-preview" in s.get("capabilities", []) else "unavailable on this compositor backend")'
printf '%s\n' 'Restart your observation daemon to use the rebuilt version, then run bridge outputs.'
