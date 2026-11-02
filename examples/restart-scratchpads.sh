#!/usr/bin/env bash
set -uo pipefail

PROJECT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
KWABAI=${KWABAI:-"$PROJECT_DIR/build/kwabai"}
GRID="32:32:7:6:18:20"

# Process pattern | desktop launcher | app regex | scratchpad name
SCRATCHPADS=(
    'https://www.icloud.com/reminders/|reminders|chrome-www\.icloud\.com__reminders_-Default|Reminders'
    'spotify|spotify-launcher|[sS]potify|Spotify'
    'thunderbird|org.mozilla.Thunderbird|org\.mozilla\.Thunderbird|Mail'
    'ThunderCalendar|thunderbird-calendar|ThunderCalendar|Calendar'
    '/usr/lib/elecwhat|elecwhat|elecwhat|whatsapp'
)

if ! "$KWABAI" doctor |
    jq -e '."bridge-connected" and ."desktop-control"' >/dev/null; then
    echo "Start the kwabai service first." >&2
    exit 1
fi

for entry in "${SCRATCHPADS[@]}"; do
    IFS='|' read -r pattern desktop class scratchpad <<< "$entry"
    "$KWABAI" -m rule --add label="scratchpad-$scratchpad" \
        app="^${class}$" scratchpad="$scratchpad" grid="$GRID" || exit 1
done

for entry in "${SCRATCHPADS[@]}"; do
    IFS='|' read -r pattern desktop class scratchpad <<< "$entry"
    pkill -f "$pattern" 2>/dev/null || true
done
sleep 1

WORKERS=()
for entry in "${SCRATCHPADS[@]}"; do
    IFS='|' read -r pattern desktop class scratchpad <<< "$entry"
    gtk-launch "$desktop" >/dev/null 2>&1 &
    (
        deadline=$((SECONDS + 30))
        sized=0
        hidden_since=-1
        settle_seconds=3

        while ((SECONDS < deadline)); do
            window=$(
                "$KWABAI" -m query --windows id,scratchpad,scratchpad-hidden |
                    jq -c --arg name "$scratchpad" \
                        'first(.[] | select(.scratchpad == $name)) // empty'
            )
            if [[ -z "$window" ]]; then
                hidden_since=-1
                sleep 0.1
                continue
            fi

            if jq -e '."scratchpad-hidden"' <<< "$window" >/dev/null; then
                ((hidden_since < 0)) && hidden_since=$SECONDS
                # Apps can reassert visibility while their startup completes.
                ((SECONDS - hidden_since >= settle_seconds)) && exit 0
                sleep 0.1
                continue
            fi
            hidden_since=-1
            id=$(jq -r '.id' <<< "$window")

            if (( ! sized )); then
                if ! "$KWABAI" -m window "$id" --grid "$GRID" >/dev/null 2>&1; then
                    sleep 0.1
                    continue
                fi
                sized=1
            fi

            # A first toggle may summon a window from another output/desktop.
            # Keep checking until it is actually hidden; never toggle blindly.
            "$KWABAI" -m window --toggle "$scratchpad" >/dev/null 2>&1 || true
            sleep 0.1
        done
        echo "Timed out waiting for $scratchpad to remain hidden." >&2
        exit 1
    ) &
    WORKERS+=("$!")
done

status=0
for pid in "${WORKERS[@]}"; do
    wait "$pid" || status=1
done
exit "$status"
